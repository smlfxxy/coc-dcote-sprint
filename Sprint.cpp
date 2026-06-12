// Sprint.asi  -- hold-to-sprint plugin for Call of Cthulhu: Dark Corners of the Earth
// Loaded by Ultimate ASI Loader (dinput8.dll). No ASLR in the game exe, so addresses are stable.
// Strategy: at startup scan committed memory for the parsed MoveSpeed floats
// (Walk=380, Run=560, Crawl=180 etc.), then while the sprint key is held multiply
// Walk & Run by a configurable factor; restore on release.

#include <windows.h>
#include <stdio.h>
#include <vector>

extern "C" IMAGE_DOS_HEADER __ImageBase;

static const float WALK_DEF  = 380.0f;
static const float RUN_DEF   = 560.0f;
static const float CRAWL_DEF = 180.0f;

static char  g_iniPath[MAX_PATH];
static char  g_logPath[MAX_PATH];
static char  g_dir[MAX_PATH];      // folder of this .asi (Engine\scripts\)

static int   g_key      = VK_LSHIFT; // sprint key
static float g_mult     = 2.0f;      // speed multiplier while held
static int   g_toggle   = 1;         // 1 = press to toggle on/off, 0 = hold-to-sprint
static unsigned g_manualWalkVA = 0;  // optional manual address override (absolute VA)

static void Log(const char* fmt, ...) {
    FILE* f = fopen(g_logPath, "a");
    if (!f) return;
    va_list ap; va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static bool almost(float a, float b) { float d = a - b; if (d < 0) d = -d; return d < 0.05f; }

// SEH-guarded float compare, isolated in its own function (no C++ unwinding objects here).
static bool SafeIsFloat(unsigned char* p, float t) {
    __try { return almost(*(float*)p, t); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Scan all committed, readable, writable regions for a float == target.
static void FindFloat(float target, std::vector<unsigned char*>& out) {
    SYSTEM_INFO si; GetSystemInfo(&si);
    unsigned char* p   = (unsigned char*)si.lpMinimumApplicationAddress;
    unsigned char* end = (unsigned char*)si.lpMaximumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi;
    while (p < end && VirtualQuery(p, &mbi, sizeof(mbi))) {
        unsigned char* base = (unsigned char*)mbi.BaseAddress;
        if (mbi.State == MEM_COMMIT &&
            (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE)) &&
            !(mbi.Protect & PAGE_GUARD)) {
            unsigned char* s = base;
            unsigned char* e = base + mbi.RegionSize - 4;
            for (; s < e; s += 4) {
                float v = *(float*)s;
                if (almost(v, target)) out.push_back(s);
            }
        }
        p = base + mbi.RegionSize;
    }
}

static const float JUMP_DEF  = 590.0f;
static const float CLIMB_DEF = 25.0f;

// The parsed config stores the speeds as a tight, ordered struct:
//   [-4]=Crawl(180) [0]=Walk(380) [+4]=Run(560) [+8]=Jump(590) [+12]=Climb(25)
// Score each Walk(380) candidate by how much of that exact signature matches.
static unsigned char* PickWalk(std::vector<unsigned char*>& walks) {
    unsigned char* best = NULL; int bestScore = 0;
    for (size_t i = 0; i < walks.size(); ++i) {
        unsigned char* a = walks[i];
        int score = 0;
        if (SafeIsFloat(a - 4, CRAWL_DEF)) score++;
        if (SafeIsFloat(a + 4, RUN_DEF))   score++;
        if (SafeIsFloat(a + 8, JUMP_DEF))  score++;
        if (SafeIsFloat(a + 12, CLIMB_DEF))score++;
        if (score >= 2) {  // at least Walk plus 2 ordered neighbours = real config
            Log("  STRONG candidate @ %p sig=%d  [crawl=%.0f walk=%.0f run=%.0f jump=%.0f climb=%.0f]",
                a, score,
                *(float*)(a-4), *(float*)a, *(float*)(a+4), *(float*)(a+8), *(float*)(a+12));
            if (score > bestScore) { bestScore = score; best = a; }
        }
    }
    Log("  picked walk @ %p (sig score=%d)", best, bestScore);
    return best;
}

static void WriteFloat(void* addr, float val) {
    DWORD old;
    if (VirtualProtect(addr, 4, PAGE_READWRITE, &old)) {
        *(float*)addr = val;
        VirtualProtect(addr, 4, old, &old);
    }
}

// --- window icon fix: the game exe has no icon -> blank taskbar entry ---
struct EnumCtx { DWORD pid; HWND hwnd; };
static BOOL CALLBACK EnumProc(HWND h, LPARAM lp) {
    EnumCtx* e = (EnumCtx*)lp; DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == e->pid && GetWindow(h, GW_OWNER) == NULL && IsWindowVisible(h)) { e->hwnd = h; return FALSE; }
    return TRUE;
}
static HWND FindGameWindow() {
    HWND h = FindWindowA("CoCWindow", NULL);
    if (h) return h;
    EnumCtx e; e.pid = GetCurrentProcessId(); e.hwnd = NULL;
    EnumWindows(EnumProc, (LPARAM)&e);
    return e.hwnd;
}
static void SetGameIcon() {
    char ico[MAX_PATH];
    sprintf(ico, "%s..\\CoCDCoTE.ico", g_dir);   // Engine\scripts\ -> Engine\CoCDCoTE.ico
    HWND hwnd = FindGameWindow();
    if (!hwnd) { Log("icon: game window not found"); return; }
    HICON big = (HICON)LoadImageA(NULL, ico, IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
    HICON sm  = (HICON)LoadImageA(NULL, ico, IMAGE_ICON, 16, 16, LR_LOADFROMFILE);
    if (big) { SendMessageA(hwnd, WM_SETICON, ICON_BIG, (LPARAM)big);   SetClassLongPtrA(hwnd, GCLP_HICON,   (LONG_PTR)big); }
    if (sm)  { SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)sm);  SetClassLongPtrA(hwnd, GCLP_HICONSM, (LONG_PTR)sm); }
    Log("icon: hwnd=%p big=%p sm=%p from '%s'", hwnd, big, sm, ico);
}

static DWORD WINAPI Worker(LPVOID) {
    // give SteamStub time to decrypt and the game to parse its config
    Sleep(8000);
    Log("=== Sprint plugin start. key=0x%X mult=%.2f manualWalkVA=0x%X ===", g_key, g_mult, g_manualWalkVA);
    SetGameIcon();

    unsigned char* walk = NULL;
    unsigned char* run  = NULL;

    if (g_manualWalkVA) {
        walk = (unsigned char*)g_manualWalkVA;
        Log("Using manual walk VA 0x%X (current value=%.2f)", g_manualWalkVA, *(float*)walk);
    } else {
        // retry the scan: the speed config may not be parsed into memory immediately
        for (int attempt = 1; attempt <= 20 && !walk; ++attempt) {
            std::vector<unsigned char*> walks;
            FindFloat(WALK_DEF, walks);
            Log("[scan %d] found %zu candidate(s) for Walk=%.1f", attempt, walks.size(), WALK_DEF);
            if (!walks.empty()) { walk = PickWalk(walks); }
            if (!walk) Sleep(3000);
        }
    }

    if (!walk) { Log("FAILED to locate walk speed. Add ManualWalkVA to Sprint.ini after finding it in Cheat Engine."); return 0; }

    // try to locate Run within +/- window relative to walk (for boosting both)
    for (int off = -512; off <= 512 && !run; off += 4) {
        if (SafeIsFloat(walk + off, RUN_DEF)) run = walk + off;
    }
    Log("walk @ %p  run @ %p", walk, run);

    float walkOrig = *(float*)walk;
    float runOrig  = run ? *(float*)run : 0.0f;
    float walkBoost = walkOrig * g_mult;
    float runBoost  = runOrig  * g_mult;
    Log("orig walk=%.1f run=%.1f  -> boost walk=%.1f run=%.1f", walkOrig, runOrig, walkBoost, runBoost);

    bool sprinting = false;
    bool prevDown  = false;
    int  logTick = 0;
    Log("mode = %s", g_toggle ? "TOGGLE" : "HOLD");
    for (;;) {
        bool down = (GetAsyncKeyState(g_key) & 0x8000) != 0;
        bool edge = down && !prevDown;   // fresh key press
        prevDown = down;

        if (g_toggle) {
            if (edge) { sprinting = !sprinting; Log("SPRINT %s", sprinting ? "ON" : "off"); }
        } else {
            if (down != sprinting) { sprinting = down; Log("SPRINT %s", sprinting ? "ON" : "off"); }
        }

        // enforce desired speed every tick (in case the game rewrites it)
        if (sprinting) {
            if (!almost(*(float*)walk, walkBoost)) WriteFloat(walk, walkBoost);
            if (run && !almost(*(float*)run, runBoost)) WriteFloat(run, runBoost);
        } else {
            if (!almost(*(float*)walk, walkOrig)) WriteFloat(walk, walkOrig);
            if (run && !almost(*(float*)run, runOrig)) WriteFloat(run, runOrig);
        }
        Sleep(10);
        if (++logTick % 1500 == 0) { Log("alive: sprint=%d walk=%.1f", (int)sprinting, *(float*)walk); SetGameIcon(); }
    }
    return 0;
}

static void LoadIni() {
    GetModuleFileNameA((HMODULE)&__ImageBase, g_iniPath, MAX_PATH); // path of this asi
    // derive ini/log paths next to the asi
    strcpy(g_dir, g_iniPath);
    char* slash = strrchr(g_dir, '\\'); if (slash) *(slash+1) = 0;
    sprintf(g_iniPath, "%sSprint.ini", g_dir);
    sprintf(g_logPath, "%sSprint.log", g_dir);

    g_key  = GetPrivateProfileIntA("Sprint", "Key", VK_LSHIFT, g_iniPath);
    char buf[64];
    GetPrivateProfileStringA("Sprint", "Multiplier", "2.0", buf, sizeof(buf), g_iniPath);
    g_mult = (float)atof(buf);
    g_toggle = GetPrivateProfileIntA("Sprint", "Toggle", 1, g_iniPath);
    GetPrivateProfileStringA("Sprint", "ManualWalkVA", "0", buf, sizeof(buf), g_iniPath);
    g_manualWalkVA = (unsigned)strtoul(buf, NULL, 0); // accepts 0x...
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        LoadIni();
        // reset log
        FILE* f = fopen(g_logPath, "w"); if (f) { fputs("Sprint.asi loaded\n", f); fclose(f); }
        CreateThread(NULL, 0, Worker, NULL, 0, NULL);
    }
    return TRUE;
}
