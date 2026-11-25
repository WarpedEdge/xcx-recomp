// The in-game menu core: Dear ImGui state, the event queue the window adapter fills, gamepad navigation, the Vulkan back
// end on our device and the menu's pages. Free of any window system (menu.h). ImGui runs on the present path only,
// under gfx_present.c's present lock; the event functions only queue.
//
// The Vulkan set-up follows LAS Recomp's runtime/nvn/vulkan/imgui_glue.cpp (https://github.com/peiikai/LASRecomp @
// da35b83a9611d046a9a681aae668d7085954d37f, MIT, Copyright (c) 2026 Peiikai): dynamic rendering into the swapchain
// image, an internal descriptor pool, vertex buffers rotated over more frames than are in flight.
//
// The look (colours, rounding, the tab bar with L / R and Q / E, the window over a dimmed game) follows
// SavageWindRecomp's runtime/ui/menu.mm (https://github.com/Revivedx/SavageWindRecomp @
// 575ad88e02f9a89a9a885d90039d48af33c982f4, MIT, Copyright (c) 2026 Revivedx and the Savage Wind Recomp contributors,
// Copyright (c) 2026 Peiikai); the pages are rows of option_rows.cpp.
#include <algorithm>
#include <atomic>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imgui.h"
#include "imgui_internal.h"   // SetFocusID, NavCursorVisible
#include "backends/imgui_impl_vulkan.h"
#include "ui/binds_page.h"
extern "C" {
#include "input_glyphs.h"
}
#include "ui/option_rows.h"
#include "ui/mods_page.h"

extern "C" {
#include "cfg.h"
#include "cheats.h"
#include "mods.h"
#include "mod_settings.h"
#include "mod_menu.h"
#include "input.h"
#include "input_map.h"
#include "input_wsys.h"
#include "perfmon.h"
#include "platform/platform.h"
#include "romfs_mods.h"
#include "swkbd.h"
#include "sysinfo.h"
#include "bug_report.h"
#include "ui/menu.h"
#include "ui_text.h"
// host.h and vk.h pull in cpu.h, which is C only: the functions the menu needs from them, as declared there.
void rt_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void vk_queue_lock(void);
void vk_queue_unlock(void);
void res_sizes(unsigned out[2], unsigned scene[2]);
double frame_step_limit_hz(void);
extern const int rt_game_version;
extern const char rt_game_name[];   // the game version shown, "2.0.0" (tools/build_game.py)
extern const char rt_port_version[];
}

// Roboto Medium (Apache-2.0, ui/fonts/LICENSE-Roboto.txt) with Latin and Cyrillic glyphs.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"   // #embed in C++
static const unsigned char roboto_ttf[] = {
#embed "fonts/Roboto-Medium.ttf"
};
#pragma clang diagnostic pop

// ---- open state, any thread
static std::atomic<int> g_open;     // the menu is open
static std::atomic<int> g_reset;    // opened or closed since the last laid-out frame: drop stale input
// The Quit Game prompt (menu_quit_prompt) is up in place of the menu: 1 it opened the menu state and closes it with No,
// 2 it came up over the open menu and No returns there. Closing the menu ends it.
static std::atomic<int> g_quit;

// g_open, the input block and the log line change together under open_lock, so they cannot disagree when the window thread
// (F1, Esc), the polling thread (combo) and the present path (Continue, B) race.
static PlatMutex open_lock = PLAT_MUTEX_INIT;

// want 0 or 1 sets the state; -1 flips it. Returns the new state.
static int change_open(int want) {
    plat_mutex_lock(&open_lock);
    int open = want < 0 ? !g_open.load() : want;
    if (g_open.load() != open) {
        g_open.store(open);
        if(!open)mod_ui_interaction(0);
        if (!open) g_quit.store(0);
        g_reset.store(1);
        input_menu(open);
        rt_log("menu: %s", open ? "open" : "closed");
    }
    plat_mutex_unlock(&open_lock);
    return open;
}

static void set_open(int open) { change_open(open); }

static int g_failed_flag();
static std::atomic<int> g_text;   // a text entry is up (menu_text_begin): it takes the input and the overlay
extern "C" int menu_is_open(void) { return g_open.load(std::memory_order_relaxed); }
extern "C" int menu_text_active(void) { return g_text.load(std::memory_order_relaxed); }
extern "C" void menu_toggle(void) {   // not over a text entry: the game is waiting for its answer
    if (!g_failed_flag() && !menu_text_active()) change_open(-1);
}

// L+R+ZL+ZR held (input_quit_gate): the prompt blocks the game's input like the menu. Not over a text entry.
extern "C" void menu_quit_prompt(void) {
    if (g_failed_flag() || menu_text_active() || g_quit.load()) return;
    g_quit.store(g_open.load() ? 2 : 1);
    g_reset.store(1);   // the held combination does not press anything in the prompt
    change_open(1);
    rt_log("menu: quit prompt");
}

// ---- events from the window adapter, queued while the menu is open; the present path hands them to ImGui
enum { EV_KEY, EV_POS, EV_BUTTON, EV_WHEEL, EV_FOCUS, EV_CHAR, EV_MAX = 256 };
struct Ev { int type, a, b; float x, y; };
static PlatMutex ev_lock = PLAT_MUTEX_INIT;
static Ev ev_q[EV_MAX];
static int ev_n;

static void push(Ev e) {
    if (!menu_is_open() && !menu_text_active()) return;
    plat_mutex_lock(&ev_lock);
    if (e.type == EV_POS && ev_n && ev_q[ev_n - 1].type == EV_POS) ev_q[ev_n - 1] = e;   // only the newest position
    else if (ev_n < EV_MAX) ev_q[ev_n++] = e;
    plat_mutex_unlock(&ev_lock);
}

extern "C" void menu_event_key(int key, int down) { push({EV_KEY, key, down, 0, 0}); }
extern "C" void menu_event_mouse_pos(float x, float y) { push({EV_POS, 0, 0, x, y}); }
extern "C" void menu_event_mouse_button(int button, int down) { push({EV_BUTTON, button, down, 0, 0}); }
extern "C" void menu_event_wheel(float dx, float dy) { push({EV_WHEEL, 0, 0, dx, dy}); }
extern "C" void menu_event_focus(int focused) { push({EV_FOCUS, focused, 0, 0, 0}); }
extern "C" void menu_event_text(const char *utf8) {
    uint16_t u[64];
    size_t n = swkbd_utf8_to_utf16(utf8, u, sizeof u / sizeof *u);
    for (size_t i = 0; i < n; i++) push({EV_CHAR, u[i], 0, 0, 0});
}

static void drain_events(ImGuiIO &io) {
    Ev q[EV_MAX];
    plat_mutex_lock(&ev_lock);
    int n = ev_n;
    for (int i = 0; i < n; i++) q[i] = ev_q[i];
    ev_n = 0;
    plat_mutex_unlock(&ev_lock);
    for (int i = 0; i < n; i++) {
        const Ev &e = q[i];
        switch (e.type) {
        case EV_KEY: io.AddKeyEvent((ImGuiKey)e.a, e.b != 0); break;
        case EV_POS: io.AddMousePosEvent(e.x, e.y); break;
        case EV_BUTTON: io.AddMouseButtonEvent(e.a, e.b != 0); break;
        case EV_WHEEL: io.AddMouseWheelEvent(e.x, e.y); break;
        case EV_FOCUS: io.AddFocusEvent(e.a != 0); break;
        case EV_CHAR: io.AddInputCharacterUTF16((ImWchar16)e.a); break;
        }
    }
}

// ---- gamepad navigation: the Switch buttons of our SDL3 gamepad to ImGui's gamepad keys. Nintendo layout: A (east)
// activates, B (south) cancels.
static const struct { int bit; ImGuiKey key; } pad_keys[] = {
    {B_A, ImGuiKey_GamepadFaceDown}, {B_B, ImGuiKey_GamepadFaceRight}, {B_X, ImGuiKey_GamepadFaceUp},
    {B_Y, ImGuiKey_GamepadFaceLeft}, {B_L, ImGuiKey_GamepadL1}, {B_R, ImGuiKey_GamepadR1},
    {B_ZL, ImGuiKey_GamepadL2}, {B_ZR, ImGuiKey_GamepadR2}, {B_LEFT, ImGuiKey_GamepadDpadLeft},
    {B_UP, ImGuiKey_GamepadDpadUp}, {B_RIGHT, ImGuiKey_GamepadDpadRight}, {B_DOWN, ImGuiKey_GamepadDpadDown},
};
static uint64_t pad_prev;   // buttons at the last frame; a button held when the menu opened counts once released
static int stick_dir = -1;  // the D-pad button the left stick holds, -1 none

// The left stick moves the cursor as the D-pad does (ImGui's own stick handling only scrolls the page): past half way
// it holds the D-pad direction of its larger axis, kept until that axis falls under 0.35; ImGui repeats a held one.
static int stick_to_dpad(int32_t lx, int32_t ly) {
    float x = lx / 32767.0f, y = ly / 32767.0f;   // y up
    float along = stick_dir == B_LEFT ? -x : stick_dir == B_RIGHT ? x : stick_dir == B_UP ? y : stick_dir == B_DOWN ? -y : 0;
    if (stick_dir >= 0 && along > 0.35f) return stick_dir;
    if (fmaxf(fabsf(x), fabsf(y)) < 0.5f) return -1;
    return fabsf(x) > fabsf(y) ? (x < 0 ? B_LEFT : B_RIGHT) : (y > 0 ? B_UP : B_DOWN);
}

static void pad_events(ImGuiIO &io, int reset) {
    uint64_t b;
    int32_t lx, ly;
    int have = input_pad_read(&b, &lx, &ly);
    if (have) io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    else io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
    stick_dir = stick_to_dpad(lx, ly);
    if (stick_dir >= 0) b |= 1ull << stick_dir;
    if (reset) {
        pad_prev = b;
        return;
    }
    for (const auto &k : pad_keys) {
        uint64_t m = 1ull << k.bit;
        if ((b ^ pad_prev) & m) io.AddKeyEvent(k.key, (b & m) != 0);
    }
    pad_prev = b;
}

// ---- ImGui and its Vulkan back end (present path)
static int g_ctx_ok;              // context made
static std::atomic<int> g_failed; // set-up failed for good (the menu stays closed)
static int g_failed_flag() { return g_failed.load(std::memory_order_relaxed); }
static int g_vk_ok;               // back end initialised for g_vk_fmt
static VkFormat g_vk_fmt;
static float g_font_scale;        // the scale of the uploaded font atlas; 0 when none is uploaded
static float g_style_scale;       // the scale the style was last sized for
static ImGuiStyle g_base_style;   // the menu's style at 1080 lines
static uint64_t g_last_ns;
static uint64_t g_cost_ns, g_cost_frames;   // CPU time of layout and recording while open, logged per 600 frames

// Sizes are tuned for 1080 lines and follow the window's height; under 720 lines they stay at the 720 size.
static float menu_scale(uint32_t h) { return (h >= 720 ? h : 720) / 1080.0f; }

// XCX's own menu look: dark navy panels, cyan accents.
static void style_init(ImGuiStyle &s) {
    ImGui::StyleColorsDark(&s);
    s.WindowRounding = 14; s.FrameRounding = 8; s.GrabRounding = 8; s.TabRounding = 8; s.ChildRounding = 10;
    s.WindowPadding = ImVec2(28, 22); s.FramePadding = ImVec2(14, 8); s.ItemSpacing = ImVec2(14, 12);
    s.WindowBorderSize = 0; s.ScrollbarSize = 18;
    ImVec4 *c = s.Colors;
    ImVec4 cyan(0.35f, 0.86f, 0.96f, 1), teal(0.11f, 0.33f, 0.45f, 1);
    c[ImGuiCol_WindowBg] = ImVec4(0.03f, 0.06f, 0.10f, 0.94f);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Text] = ImVec4(0.92f, 0.96f, 1.00f, 1);
    c[ImGuiCol_TextDisabled] = ImVec4(0.55f, 0.62f, 0.68f, 1);
    c[ImGuiCol_Button] = ImVec4(0.07f, 0.15f, 0.22f, 1);
    c[ImGuiCol_ButtonHovered] = teal;
    c[ImGuiCol_ButtonActive] = ImVec4(0.10f, 0.50f, 0.64f, 1);
    c[ImGuiCol_Tab] = ImVec4(0.06f, 0.12f, 0.18f, 1);
    c[ImGuiCol_TabHovered] = teal;
    c[ImGuiCol_TabSelected] = ImVec4(0.10f, 0.29f, 0.41f, 1);
    c[ImGuiCol_TabSelectedOverline] = cyan;
    c[ImGuiCol_NavCursor] = cyan;
    c[ImGuiCol_Separator] = ImVec4(0.20f, 0.32f, 0.40f, 1);
}

static void make_context(void) {
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.BackendPlatformName = "xcx_menu";
    style_init(g_base_style);
    ImGui::GetStyle() = g_base_style;
    g_ctx_ok = 1;
}

static void vk_down(void) {
    if (!g_vk_ok) return;
    ImGui_ImplVulkan_Shutdown();
    g_vk_ok = 0;
    g_font_scale = 0;
}

static int vk_up(const MenuTarget *t) {
    if (g_vk_ok && g_vk_fmt == t->format) return 1;
    if (g_vk_ok) {   // the swapchain changed format: pipelines for the new one
        vk_queue_lock();
        vkQueueWaitIdle(t->queue);
        vk_queue_unlock();
        vk_down();
    }
    ImGui_ImplVulkan_InitInfo ii = {};
    ii.ApiVersion = VK_API_VERSION_1_3;
    ii.Instance = t->instance;
    ii.PhysicalDevice = t->phys;
    ii.Device = t->device;
    ii.QueueFamily = t->queue_family;
    ii.Queue = t->queue;
    ii.DescriptorPoolSize = 16;
    ii.MinImageCount = 2;
    ii.ImageCount = 4;   // vertex buffers rotate per recorded frame: more than gfx_present.c's 2 frames in flight
    ii.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ii.UseDynamicRendering = true;
    g_vk_fmt = t->format;
    ii.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    ii.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    ii.PipelineRenderingCreateInfo.pColorAttachmentFormats = &g_vk_fmt;
    if (!ImGui_ImplVulkan_Init(&ii)) return 0;
    g_vk_ok = 1;
    return 1;
}

// Roboto Medium (Latin, Cyrillic, dashes and quotes) at 26 px per 1080 lines, with the glyphs of the Japanese, Korean
// and Chinese language names merged in from a system font when there is one (each language is named in its own script).
// Made again when the window height changes; the old texture goes after the queue is idle (CreateFontsTexture).
static const ImWchar kRanges[] = {0x0020, 0x00FF, 0x0400, 0x052F, 0x2010, 0x2027, 0};
static const struct { const char *lang; UiTextId name; } kCjk[] = {
    {"ja", UI_MENU_LANGNAME_JA}, {"ko", UI_MENU_LANGNAME_KO}, {"zh-Hans", UI_MENU_LANGNAME_ZH_HANS},
    {"zh-Hant", UI_MENU_LANGNAME_ZH_HANT},
};

static int fonts_up(float scale) {
    enum { N = sizeof kCjk / sizeof *kCjk };
    ImFontAtlas *fa = ImGui::GetIO().Fonts;
    fa->Clear();
    ImFontConfig fc;
    fc.FontDataOwnedByAtlas = false;
    fa->AddFontFromMemoryTTF((void *)roboto_ttf, (int)sizeof roboto_ttf, 26.0f * scale, &fc, kRanges);
    uint8_t *data[N] = {};
    ImVector<ImWchar> ranges[N];
    int merged = 0;
    for (int i = 0; i < N; i++) {
        char path[512];
        uint64_t size = 0;
        if (!plat_font_path(kCjk[i].lang, path, sizeof path) || !(data[i] = plat_file_read(path, &size))) continue;
        merged++;
        ImFontGlyphRangesBuilder b;
        b.AddText(ui_text_in("en", kCjk[i].name));
        b.BuildRanges(&ranges[i]);
        ImFontConfig mc;
        mc.MergeMode = true;
        mc.FontDataOwnedByAtlas = false;
        fa->AddFontFromMemoryTTF(data[i], (int)size, 26.0f * scale, &mc, ranges[i].Data);
    }
    vk_queue_lock();   // the upload submits on the queue and waits for it
    int ok = ImGui_ImplVulkan_CreateFontsTexture();
    vk_queue_unlock();
    fa->ClearInputData();   // the glyphs are in the texture: the font files and the pixels are not needed again
    fa->ClearTexData();
    for (uint8_t *d : data) free(d);
    rt_log("menu: font atlas at %.1f px, language names from %d of %d system fonts", 26.0f * scale, merged, (int)N);
    g_font_scale = ok ? scale : 0;
    return ok;
}

extern "C" void menu_vk_release(void) {
    mod_menu_invalidate();
    mod_ui_interaction(0);
    if (g_ctx_ok) vk_down();
}

// ---- test hooks
// XCX_MENU_TEST=<frame>[:<frames per page>]: at that presented frame the menu opens on its first tab; each tab stays
// <frames per page> frames (default 30) and its last frame's swapchain image goes to XCX_MENU_TEST_DIR (default: the
// working directory) as menu-test-<frame>-<page>.png; after the last tab the menu closes.
// XCX_MENU_TEST_SET=<frame>:<key>=<value>[,<key>=<value>...][;<frame>:...]: cfg_set of each key (XCX_ prefix
// optional) at that presented frame; changes live settings with no input, menu open or not. xcx.ini is written only by
// the pseudo-key SAVE=1 (cfg_save, as a row change does; a failed write shows the menu's save note).
// XCX_BUG_REPORT_TEST=<frame>[:save]: at that presented frame, what "Make a bug report" does (with the save for :save,
// whatever the opt-in row says), without opening the folder; the Advanced tab then scrolls to the button, so an
// XCX_MENU_TEST page shot after it shows the zip's path.
enum { TAB_SCREEN, TAB_GAME, TAB_SOUND, TAB_CONTROLS, TAB_MENU, TAB_ADVANCED, TAB_MODS, TAB_CHEATS, TAB_COUNT };
enum { TEST_PAGE_FRAMES = 30, TEST_SETS = 16 };
struct TestSet { uint64_t frame; char key[64], value[64]; };
static struct {
    int parsed;
    uint64_t at;          // XCX_MENU_TEST frame; 0 none
    uint64_t per_page;
    uint64_t opened;      // the frame the hook opened the menu; 0 not yet
    int page;             // the tab being shot; -1 none
    TestSet sets[TEST_SETS];
    int n_sets, next_set; // XCX_MENU_TEST_SET, in frame order
    uint64_t report_at;   // XCX_BUG_REPORT_TEST frame; 0 none or done
    bool report_save;
} g_test = {0, 0, 0, 0, -1, {}, 0, 0, 0, false};

static void test_parse(void) {
    g_test.parsed = 1;
    if (const char *e = cfg_get("XCX_MENU_TEST")) {
        long long at = atoll(e), per = 0;
        if (const char *c = strchr(e, ':')) per = atoll(c + 1);
        g_test.at = at > 0 ? (uint64_t)at : 0;
        g_test.per_page = per > 0 ? (uint64_t)per : TEST_PAGE_FRAMES;
    }
    if (const char *e = cfg_get("XCX_BUG_REPORT_TEST")) {
        long long at = atoll(e);
        g_test.report_at = at > 0 ? (uint64_t)at : 0;
        g_test.report_save = strstr(e, ":save") != nullptr;
    }
    const char *s = cfg_get("XCX_MENU_TEST_SET");
    while (s && *s) {   // groups "<frame>:<key>=<value>,..." separated by ';'
        char *end;
        unsigned long long f = strtoull(s, &end, 10);
        if (*end != ':') break;
        s = end + 1;
        while (*s && *s != ';' && g_test.n_sets < TEST_SETS) {
            size_t n = strcspn(s, ",;");
            const char *eq = (const char *)memchr(s, '=', n);
            if (eq) {
                TestSet &t = g_test.sets[g_test.n_sets++];
                t.frame = f;
                int kn = (int)(eq - s), vn = (int)(s + n - eq - 1);
                snprintf(t.key, sizeof t.key, "%s%.*s", strncmp(s, "XCX_", 4) ? "XCX_" : "", kn, s);
                snprintf(t.value, sizeof t.value, "%.*s", vn, eq + 1);
            }
            s += n;
            if (*s == ',') s++;
        }
        while (*s && *s != ';') s++;
        if (*s == ';') s++;
    }
    if (g_test.at || g_test.n_sets)
        rt_log("menu: test hook: open at frame %llu, %llu frames per page, %d settings changes",
               (unsigned long long)g_test.at, (unsigned long long)g_test.per_page, g_test.n_sets);
}

static void test_apply_sets(uint64_t frame) {
    for (; g_test.next_set < g_test.n_sets && g_test.sets[g_test.next_set].frame <= frame; g_test.next_set++) {
        const TestSet &t = g_test.sets[g_test.next_set];
        rt_log("menu: test hook: frame %llu: %s=%s", (unsigned long long)frame, t.key, t.value);
        if (!strcmp(t.key, "XCX_SAVE")) rt_log("menu: test hook: cfg_save %s", cfg_save() ? "wrote" : "failed");
        else cfg_set(t.key, t.value);
    }
}

// ---- the lists the Screen and Game tabs build
enum { MAX_GPUS = 8 };
static char g_gpu_names[MAX_GPUS][VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
static int g_gpu_n = -1;   // -1: not listed yet

// The Vulkan devices in vkEnumeratePhysicalDevices order, which XCX_GPU_INDEX counts in (vk_device.c pick_device).
static void list_gpus(VkInstance inst) {
    if (g_gpu_n >= 0) return;
    VkPhysicalDevice pd[MAX_GPUS];
    uint32_t n = MAX_GPUS;
    if (vkEnumeratePhysicalDevices(inst, &n, pd) < 0) n = 0;   // VK_INCOMPLETE keeps the first MAX_GPUS
    for (uint32_t i = 0; i < n; i++) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(pd[i], &p);
        snprintf(g_gpu_names[i], sizeof g_gpu_names[i], "%s", p.deviceName);
    }
    g_gpu_n = (int)n;
}

// The sizes of the two resolution rows. Window sizes fit the monitor, but for the 1920x1080 default; the game resolution
// has no such limit. 1280x720 is the Steam Deck's (letterboxed in its 1280x800 screen, XCX_PICTURE_FIT). Sizes of another
// shape are listed only for a monitor of that shape, the monitor's own size and the current value always.
static const int kWindowSizes[][2] = {{1280, 720}, {1366, 768}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3200, 1800},
                                      {3840, 2160}};
static const int kOutputSizes[][2] = {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3200, 1800}, {3840, 2160}};
static const int kWideSizes[][2] = {{2560, 1080}, {3440, 1440}, {3840, 1080}, {5120, 1440}, {5160, 2160}};
enum { MAX_SIZES = sizeof kWindowSizes / sizeof *kWindowSizes + sizeof kWideSizes / sizeof *kWideSizes + 2 };

// Within 2%: 2560x1080 and 3440x1440 are both sold as 21:9.
static bool same_shape(int w, int h, int mw, int mh) {
    long long a = (long long)w * mh, b = (long long)h * mw;
    return llabs(a - b) * 50 <= b;
}

// Adds a size where its area puts it, once.
static int size_add(int (*sz)[2], int n, int w, int h) {
    for (int i = 0; i < n; i++)
        if (sz[i][0] == w && sz[i][1] == h) return n;
    int at = n;
    while (at > 0 && (long long)sz[at - 1][0] * sz[at - 1][1] > (long long)w * h) at--;
    memmove(sz[at + 1], sz[at], sizeof sz[0] * (n - at));
    sz[at][0] = w, sz[at][1] = h;
    return n + 1;
}

static void row_sizes(const char *key, UiTextId label, UiTextId desc, const int (*sizes)[2], int count, bool fit) {
    int sz[MAX_SIZES][2], n = 0, mw = 0, mh = 0, cw = 0, ch = 0;
    int have = gfx_window_monitor_size(&mw, &mh);
    for (int i = 0; i < count; i++) {
        int w = sizes[i][0], h = sizes[i][1];
        if (fit && have && (w > mw || h > mh) && !(w == 1920 && h == 1080)) continue;
        n = size_add(sz, n, w, h);
    }
    for (size_t i = 0; have && i < sizeof kWideSizes / sizeof *kWideSizes; i++) {
        int w = kWideSizes[i][0], h = kWideSizes[i][1];
        if (!same_shape(w, h, mw, mh) || (fit && (w > mw || h > mh))) continue;
        n = size_add(sz, n, w, h);
    }
    if (have && mw <= 7680 && mh <= 4320) n = size_add(sz, n, mw, mh);   // resolution.c's limit
    if (sscanf(cfg_value(key), "%dx%d", &cw, &ch) == 2) n = size_add(sz, n, cw, ch);
    char vals[MAX_SIZES][24], shown[MAX_SIZES][32];
    const char *pv[MAX_SIZES], *pn[MAX_SIZES];
    for (int i = 0; i < n; i++) {
        snprintf(vals[i], sizeof vals[i], "%dx%d", sz[i][0], sz[i][1]);
        snprintf(shown[i], sizeof shown[i], "%d × %d", sz[i][0], sz[i][1]);   // U+00D7, the multiplication sign
        pv[i] = vals[i];
        pn[i] = shown[i];
    }
    opt_choice_list(key, label, pv, pn, n, desc);
}

static void row_window_size(void) {
    row_sizes("XCX_WINDOW_SIZE", UI_MENU_WINDOW_SIZE, UI_MENU_WINDOW_SIZE_DESC, kWindowSizes,
              sizeof kWindowSizes / sizeof *kWindowSizes, true);
}

// The game resolution (resolution.c).
static void row_output_res(void) {
    row_sizes("XCX_OUTPUT_RES", UI_MENU_OUTPUT_RES, UI_MENU_OUTPUT_RES_DESC, kOutputSizes,
              sizeof kOutputSizes / sizeof *kOutputSizes, false);
}

static void row_render_scale(void) {
    static const char *const vals[] = {"game", "50", "67", "75", "100"};
    enum { N = sizeof vals / sizeof *vals };
    char shown[N][32];
    const char *names[N];
    names[0] = menu_text(UI_MENU_RENDER_SCALE_GAME);
    for (int i = 1; i < N; i++) {
        snprintf(shown[i], sizeof shown[i], "%s %%", vals[i]);
        names[i] = shown[i];
    }
    opt_choice_list("XCX_RENDER_SCALE", UI_MENU_RENDER_SCALE, vals, names, N, UI_MENU_RENDER_SCALE_DESC);
}

static void row_gpu(void) {
    const char *vals[MAX_GPUS + 1], *names[MAX_GPUS + 1];
    char idx[MAX_GPUS][4];
    vals[0] = "";
    names[0] = menu_text(UI_MENU_GPU_AUTO);
    for (int i = 0; i < g_gpu_n; i++) {
        snprintf(idx[i], sizeof idx[i], "%d", i);
        vals[i + 1] = idx[i];
        names[i + 1] = g_gpu_names[i];
    }
    opt_choice_list("XCX_GPU_INDEX", UI_MENU_GPU, vals, names, g_gpu_n + 1, UI_MENU_GPU_DESC);
}

// The game language: one entry per language XCX ships (a Switch code that names it), each under its own name. The
// Switch's Portuguese is not among them, nor Russian: the game stops before the title in it (ov_system.c playable).
// Chinese takes the codes zh-Hans and zh-Hant, which the game knows; it stops the same way with zh-CN and zh-TW.
static void row_game_lang(void) {
    static const struct { const char *code; UiTextId name; } langs[] = {
        {"en-US", UI_MENU_LANGNAME_EN}, {"ja", UI_MENU_LANGNAME_JA}, {"fr", UI_MENU_LANGNAME_FR},
        {"de", UI_MENU_LANGNAME_DE}, {"it", UI_MENU_LANGNAME_IT}, {"es", UI_MENU_LANGNAME_ES},
        {"nl", UI_MENU_LANGNAME_NL},
        {"zh-Hans", UI_MENU_LANGNAME_ZH_HANS}, {"zh-Hant", UI_MENU_LANGNAME_ZH_HANT}, {"ko", UI_MENU_LANGNAME_KO},
    };
    enum { N = sizeof langs / sizeof *langs + 1 };
    const char *vals[N], *names[N];
    vals[0] = "";
    names[0] = menu_text(UI_MENU_GAME_LANG_AUTO);
    const char *now = cfg_value("XCX_LANG");
    int cur = *now ? -1 : 0;
    for (int i = 1; i < N; i++) {
        vals[i] = langs[i - 1].code;
        names[i] = menu_text(langs[i - 1].name);
        if (*now && !strcmp(ui_text_lang(now), ui_text_lang(vals[i]))) cur = i;   // en-GB is English too
    }
    opt_choice_list("XCX_LANG", UI_MENU_GAME_LANG, vals, names, N, UI_MENU_GAME_LANG_DESC, cur);
}

// The frame limit (frame_rate.c): unset, the older fps60 = 0 still means 30. The monitor choice shows the rate it gives.
static void row_fps_limit() {
    // 40 and 45: the Steam Deck's screen runs at 40 to 60 Hz (the OLED to 90); 40 divides 40 and 120 Hz evenly
    static const char *const vals[] = {"30", "40", "45", "60", "90", "120", "144", "display"};
    static const UiTextId ids[] = {UI_MENU_FPS_30, UI_MENU_FPS_40,  UI_MENU_FPS_45,  UI_MENU_FPS_60,
                                   UI_MENU_FPS_90, UI_MENU_FPS_120, UI_MENU_FPS_144, UI_MENU_FPS_DISPLAY};
    enum { N = sizeof vals / sizeof *vals };
    const char *names[N];
    for (int i = 0; i < N; i++) names[i] = menu_text(ids[i]);
    static char display[64];
    double hz = gfx_window_refresh_hz();
    if (hz > 0) {
        snprintf(display, sizeof display, menu_text(UI_MENU_FPS_DISPLAY_HZ), hz);
        names[N - 1] = display;
    }
    const char *set = cfg_get("XCX_FPS_LIMIT"), *old = cfg_get("XCX_FPS60");
    int cur = (!set || !*set) && old && !strcmp(old, "0") ? 0 : -1;
    opt_choice_list("XCX_FPS_LIMIT", UI_MENU_FPS_LIMIT, vals, names, N, UI_MENU_FPS_LIMIT_DESC, cur);
}

// The menu's name of a XCX_VK_PRESENT mode (gfx_window_present_mode); NULL for another.
static const char *present_name(const char *mode) {
    static const struct { const char *mode; UiTextId name; } names[] = {
        {"mailbox", UI_MENU_PRESENT_MAILBOX}, {"fifo", UI_MENU_PRESENT_VSYNC}, {"immediate", UI_MENU_PRESENT_IMMEDIATE},
    };
    for (const auto &n : names)
        if (!strcmp(n.mode, mode)) return menu_text(n.name);
    return nullptr;
}

// "in use: VSync" under the Presentation value when the swapchain got another mode than the setting (a driver without
// mailbox gets fifo); NULL when they agree or before the first swapchain.
static const char *present_note(void) {
    const char *got = gfx_window_present_mode(), *name = present_name(got);
    if (!*got || !strcmp(got, cfg_value("XCX_VK_PRESENT")) || !name) return nullptr;
    static char note[96];
    snprintf(note, sizeof note, menu_text(UI_MENU_PRESENT_IN_USE), name);
    return note;
}

// ---- the tabs
static void tab_screen(float scale) {
    opt_begin("##screen", scale);
    static const UiTextId mode[] = {UI_MENU_MODE_WINDOW, UI_MENU_MODE_BORDERLESS, UI_MENU_MODE_FULLSCREEN};
    opt_choice("XCX_FULLSCREEN", UI_MENU_FULLSCREEN, mode, 3, UI_MENU_FULLSCREEN_DESC);
    row_window_size();
    static const UiTextId present[] = {UI_MENU_PRESENT_MAILBOX, UI_MENU_PRESENT_VSYNC, UI_MENU_PRESENT_IMMEDIATE};
    opt_choice("XCX_VK_PRESENT", UI_MENU_PRESENT, present, 3, UI_MENU_PRESENT_DESC, present_note());
    row_gpu();
    static const UiTextId perf[] = {UI_MENU_PERF_OFF, UI_MENU_PERF_FPS, UI_MENU_PERF_FULL};
    opt_choice("XCX_PERF_OVERLAY", UI_MENU_PERF_OVERLAY, perf, 3, UI_MENU_PERF_OVERLAY_DESC);
    opt_group(UI_MENU_GRAPHICS);
    row_output_res();
    row_render_scale();
    static const UiTextId up[] = {UI_MENU_UPSCALER_FSR, UI_MENU_UPSCALER_BILINEAR, UI_MENU_UPSCALER_DLSS};
    opt_choice("XCX_UPSCALER", UI_MENU_UPSCALER, up, 3, UI_MENU_UPSCALER_DESC);
    static const UiTextId fit[] = {UI_MENU_PICTURE_FIT_KEEP, UI_MENU_PICTURE_FIT_STRETCH};
    opt_choice("XCX_PICTURE_FIT", UI_MENU_PICTURE_FIT, fit, 2, UI_MENU_PICTURE_FIT_DESC);
    opt_slider("XCX_SHARPNESS", UI_MENU_SHARPNESS, 0, 100, UI_MENU_SHARPNESS_DESC, 5, "%.0f %%");
    opt_slider("XCX_BRIGHTNESS", UI_MENU_BRIGHTNESS, 40, 120, UI_MENU_BRIGHTNESS_DESC, 5, "%.0f %%");
    opt_group(UI_MENU_HDR_GROUP);
    opt_toggle("XCX_HDR", UI_MENU_HDR, UI_MENU_HDR_DESC);
    opt_slider("XCX_HDR_PEAK_NITS", UI_MENU_HDR_PEAK, 400, 10000, UI_MENU_HDR_PEAK_DESC, 50, "%.0f");
    opt_slider("XCX_HDR_GAME_NITS", UI_MENU_HDR_GAME, 80, 500, UI_MENU_HDR_GAME_DESC, 5, "%.0f");
    opt_slider("XCX_HDR_UI_NITS", UI_MENU_HDR_UI, 80, 500, UI_MENU_HDR_UI_DESC, 5, "%.0f");
    opt_slider("XCX_HDR_HUE", UI_MENU_HDR_HUE, 0, 100, UI_MENU_HDR_HUE_DESC, 5, "%.0f %%");
    opt_end();
}

static void tab_game(float scale) {
    opt_begin("##game", scale);
    row_fps_limit();
    static const UiTextId cut[] = {UI_MENU_CUTSCENE_30, UI_MENU_CUTSCENE_MIXED, UI_MENU_CUTSCENE_LIMIT};
    opt_choice("XCX_CUTSCENE_30", UI_MENU_CUTSCENE_FPS, cut, 3, UI_MENU_CUTSCENE_FPS_DESC);
    row_game_lang();
    static const UiTextId voice[] = {UI_MENU_GAME_LANG_AUTO, UI_MENU_VOICE_JA, UI_MENU_VOICE_EN};
    opt_choice("XCX_VOICE_LANG", UI_MENU_VOICE_LANG, voice, 3, UI_MENU_VOICE_LANG_DESC);
    static const UiTextId mode[] = {UI_MENU_MODE_DOCKED, UI_MENU_MODE_HANDHELD};
    opt_choice("XCX_OPERATION_MODE", UI_MENU_OPERATION_MODE, mode, 2, UI_MENU_OPERATION_MODE_DESC);
    opt_toggle("XCX_CHEATS", UI_MENU_CHEATS, UI_MENU_CHEATS_DESC);
    opt_end();
}

static void tab_sound(float scale) {
    opt_begin("##sound", scale);
    opt_slider("XCX_VOLUME", UI_MENU_VOLUME, 0, 100, UI_MENU_VOLUME_DESC, 5, "%.0f %%");
    opt_end();
}

static void tab_controls(float scale) {
    opt_begin("##controls", scale);
    static const char *const glyph_values[] = {"auto","original","playstation","xbox","steam","keyboard"};
    static const char *const glyph_names[] = {"Automatic","Nintendo Switch","PlayStation","Xbox","Steam Controller","Keyboard/Mouse"};
    if (!glyph_build_supported()) opt_disable_next(UI_MENU_GLYPH_UNSUPPORTED);
    opt_choice_list("XCX_GLYPH_STYLE", UI_MENU_GLYPH_STYLE, glyph_values, glyph_names, GLYPH_STYLE_COUNT,
                    UI_MENU_GLYPH_STYLE_DESC);
    static const char *const appearance_values[] = {"colored","standard"};
    static const char *const appearance_names[] = {"Colored","Standard"};
    if (!glyph_build_supported()) opt_disable_next(UI_MENU_GLYPH_UNSUPPORTED);
    opt_choice_list("XCX_GLYPH_APPEARANCE", UI_MENU_GLYPH_APPEARANCE, appearance_values, appearance_names, 2,
                    UI_MENU_GLYPH_APPEARANCE_DESC);
    static const UiTextId layout[] = {UI_MENU_PAD_LAYOUT_AUTO, UI_MENU_PAD_LAYOUT_NINTENDO, UI_MENU_PAD_LAYOUT_XBOX};
    opt_choice("XCX_PAD_LAYOUT", UI_MENU_PAD_LAYOUT, layout, 3, UI_MENU_PAD_LAYOUT_DESC);
    opt_toggle("XCX_MOTION", UI_MENU_MOTION, UI_MENU_MOTION_DESC);
    opt_slider("XCX_RUMBLE_STRENGTH", UI_MENU_RUMBLE_STRENGTH, 0, 300, UI_MENU_RUMBLE_STRENGTH_DESC, 10, "%.0f %%");
    opt_slider("XCX_RUMBLE_CURVE", UI_MENU_RUMBLE_CURVE, 0.2f, 1.5f, UI_MENU_RUMBLE_CURVE_DESC, 0.05f, "%.2f");
    opt_toggle("XCX_RUMBLE_TRIGGERS", UI_MENU_RUMBLE_TRIGGERS, UI_MENU_RUMBLE_TRIGGERS_DESC);
    opt_slider("XCX_MOUSE_SENS", UI_MENU_MOUSE_SENS, 0, 10, UI_MENU_MOUSE_SENS_DESC, 0.1f, "%.1f");
    binds_rows(scale);
    opt_end();
    binds_after_page();
}

static void tab_menu(float scale) {
    opt_begin("##menu", scale);
    static const UiTextId langs[] = {UI_MENU_MENU_LANG_AUTO, UI_MENU_LANGNAME_EN, UI_MENU_LANGNAME_RU};
    opt_choice("XCX_MENU_LANG", UI_MENU_MENU_LANG, langs, 3, UI_MENU_MENU_LANG_DESC);
    opt_end();
}

// Built-in cheats whose description carries a warning (and their row a mark): they can break a save's progress.
static UiTextId cheat_warning(const CheatRow &c) {
    if (strcmp(c.source, CHEATS_BUILTIN)) return UI_TEXT_COUNT;
    // A chest of another colour can take the place of one that holds a mission's key item.
    if (!strcmp(c.name, "Silver Chests Always") || !strcmp(c.name, "Gold Chests Always")) return UI_CHEATS_CHEST_WARN;
    return UI_TEXT_COUNT;
}

// ---- Cheats: the built-in cheats or each mod folder's (cheats.c), one list at a time when there are both, so a
// player's own cheats are not at the end of the long built-in list. Nothing here is a setting: the rows are built from
// the cheat files and cheats-enabled.txt keeps them.
static void tab_cheats(float scale) {
    opt_begin("##cheats", scale);
    char line[4400];
    int n = cheats_count();
    if (n) opt_text(UI_CHEATS_WARN, true);
    else opt_text(UI_CHEATS_NONE);
    int counts[2] = {0, 0};   // rows a player can see: built-in, from cheat files
    for (int i = 0; i < n; i++) {
        CheatRow c = cheats_row(i);
        size_t len_name = strlen(c.name);
        bool label = len_name > 8 && !strncmp(c.name, "--- ", 4) && !strcmp(c.name + len_name - 4, " ---");
        if (!label && c.state != CHEAT_HIDDEN) counts[strcmp(c.source, CHEATS_BUILTIN) != 0]++;
    }
    static int shown = -1;   // 0 built-in, 1 cheat files; first the player's own when there are any
    if (shown < 0 || !counts[shown]) shown = counts[1] ? 1 : 0;
    if (counts[0] && counts[1]) {
        char value[96];
        snprintf(value, sizeof value, menu_text(shown ? UI_CHEATS_LIST_FILES : UI_CHEATS_LIST_BUILTIN), counts[shown]);
        if (opt_button("##cheats_list", UI_CHEATS_LIST, UI_CHEATS_LIST_DESC, value)) shown ^= 1;
    }
    const char *group = NULL;
    for (int i = 0; i < n; i++) {
        CheatRow c = cheats_row(i);
        if ((strcmp(c.source, CHEATS_BUILTIN) != 0) != (shown == 1)) continue;
        if (!group || strcmp(group, c.source)) {   // a heading per source, as the rows come in source order
            group = c.source;
            ImGui::Dummy(ImVec2(0, 6 * scale));
            opt_text_raw(c.source);
            if (!strcmp(c.source, CHEATS_BUILTIN)) opt_text(UI_CHEATS_CREDITS);
        }
        size_t len_name = strlen(c.name);   // "--- Combat ---": a group label of the cheat list
        if (len_name > 8 && !strncmp(c.name, "--- ", 4) && !strcmp(c.name + len_name - 4, " ---")) {
            snprintf(line, sizeof line, "%.*s", (int)(len_name - 8), c.name + 4);
            opt_group_raw(line);
            continue;
        }
        if (c.state == CHEAT_HIDDEN) continue;   // a code cave, a restore or a note: nothing of its own to switch
        char id[32], desc[1024];
        snprintf(id, sizeof id, "##cheat%d", i);
        int builtin = !strcmp(c.source, CHEATS_BUILTIN);
        bool runs = c.state == CHEAT_OK || c.state == CHEAT_PATCHED;
        int len = snprintf(desc, sizeof desc, "%s",
                           menu_text(c.state == CHEAT_WRITES_CODE ? UI_CHEATS_CODE
                                     : c.state == CHEAT_NOT_BUILT ? UI_CHEATS_NOT_BUILT
                                     : c.state == CHEAT_UNSUPPORTED ? UI_CHEATS_UNSUPPORTED
                                     : c.state == CHEAT_MALFORMED ? UI_CHEATS_BAD
                                     : c.master                   ? UI_CHEATS_MASTER
                                     : c.refused                  ? UI_CHEATS_REFUSED
                                     : c.state == CHEAT_PATCHED   ? UI_CHEATS_PATCHED
                                     : builtin                    ? UI_CHEATS_BUILTIN
                                                                  : UI_CHEATS_DESC));
        if (len > 0 && len < (int)sizeof desc && c.state == CHEAT_UNSUPPORTED && *c.note)
            len += snprintf(desc + len, sizeof desc - len, "\n\n%s", c.note);
        if (len > 0 && len < (int)sizeof desc && c.buttons)
            len += snprintf(desc + len, sizeof desc - len, menu_text(UI_CHEATS_BUTTONS), (unsigned)c.buttons);
        UiTextId warning = cheat_warning(c);
        char label[600];
        snprintf(label, sizeof label, "%s", c.name);
        if (warning != UI_TEXT_COUNT) {
            if (len > 0 && len < (int)sizeof desc) snprintf(desc + len, sizeof desc - len, "%s", menu_text(warning));
            snprintf(label, sizeof label, "%s %s", c.name, menu_text(UI_CHEATS_WARNING_MARK));
        }
        if (c.state == CHEAT_WRITES_CODE || c.state == CHEAT_UNSUPPORTED) opt_disable_next(UI_CHEATS_NOTE_CODE);
        else if (c.state == CHEAT_NOT_BUILT) opt_disable_next(UI_CHEATS_NOTE_SETUP);
        else if (c.state == CHEAT_MALFORMED) opt_disable_next(UI_CHEATS_NOTE_BAD);
        else if (c.master) opt_disable_next(UI_CHEATS_NOTE_MASTER);
        if (opt_row_text(id, label, desc, (c.enabled || c.master) && runs) && runs && !c.master)
            cheats_set(i, !c.enabled);
    }
    opt_group(UI_CHEATS_WHERE);
    if (opt_button("##cheats_reload", UI_CHEATS_RELOAD, UI_CHEATS_RELOAD_DESC)) cheats_reload();
    if (cheats_other_builds()) {
        snprintf(line, sizeof line, menu_text(UI_CHEATS_OTHER_BUILDS), cheats_other_builds());
        opt_text_raw(line);
    }
    snprintf(line, sizeof line, menu_text(UI_CHEATS_BUILD), *cheats_build_id() ? cheats_build_id() : "?");
    opt_text_raw(line);
    const char *dir = romfs_mods_dir();
    if (dir) {
        snprintf(line, sizeof line, "%s", dir);
        opt_text_raw(line);
    }
    opt_end();
}

// ---- Advanced: the testing and troubleshooting keys, the bug report helpers and the reset of every setting
static bool g_modal;   // the reset confirmation is open: B and Esc close it, not the menu; L / R stay on the page

// A thread count with no fixed default: Automatic (unset), then the common counts.
static void row_threads(const char *key, UiTextId label, UiTextId desc) {
    static const char *const vals[] = {"", "1", "2", "4", "6", "8", "12", "16", "24", "32", "48", "64"};
    enum { N = sizeof vals / sizeof *vals };
    const char *names[N];
    names[0] = menu_text(UI_MENU_AUTO);
    for (int i = 1; i < N; i++) names[i] = vals[i];
    opt_choice_list(key, label, vals, names, N, desc);
}

// A free-text key shown as Off (unset) / On (value set): On writes on_value, any set value reads as On.
static void row_set_or_not(const char *key, UiTextId label, UiTextId desc, const char *on_value) {
    const char *vals[] = {"", on_value}, *names[] = {menu_text(UI_MENU_OFF), menu_text(UI_MENU_ON)};
    opt_choice_list(key, label, vals, names, 2, desc, *cfg_value(key) ? 1 : 0);
}

static bool validation_layer_present(void) {
    static int have = -1;
    if (have < 0) {
        uint32_t n = 0;
        vkEnumerateInstanceLayerProperties(&n, nullptr);
        VkLayerProperties *l = (VkLayerProperties *)calloc(n ? n : 1, sizeof *l);
        if (l && vkEnumerateInstanceLayerProperties(&n, l) >= 0)
            for (uint32_t i = 0; i < n && have <= 0; i++) have = !strcmp(l[i].layerName, "VK_LAYER_KHRONOS_validation");
        free(l);
        if (have < 0) have = 0;
    }
    return have != 0;
}

// Every setting the menu shows back to its default (the environment's keys stay), written to xcx.ini.
static void reset_all(void) {
    const char *keys[128];
    int n = 0;
    for (const CfgMeta *m; n < 128 && (m = cfg_meta_at(n)); n++) keys[n] = m->key;
    int done = cfg_reset(keys, n);
    opt_save_soon();
    rt_log("menu: reset %d settings to their defaults", done);
}

// A button's result shown in its value for a few seconds: 1 done (done_text), -1 failed.
struct ActionNote { uint64_t until; int result; };

static const char *action_value(const ActionNote &a, UiTextId done_text) {
    if (!a.result || plat_now_ns() > a.until) return "";
    return menu_text(a.result > 0 ? done_text : UI_MENU_ACTION_FAILED);
}

static void action_done(ActionNote &a, bool ok) { a = {plat_now_ns() + 3000000000ull, ok ? 1 : -1}; }

// "Make a bug report": bug_report_write on a thread of its own (a long log takes a while to pack), then the folder opens
// and the zip's path shows under the button until the game ends. The save goes in only when the opt-in row is on,
// which starts off in every run.
enum { REPORT_IDLE, REPORT_BUSY, REPORT_DONE, REPORT_FAILED };
static struct {
    std::atomic<int> state{REPORT_IDLE};
    bool with_save, open_folder;
    char *info;                       // sysinfo_text, made on the menu's thread
    char path[PLAT_PATH_MAX * 2];
} g_report;
static bool g_report_save;
static int g_report_focus;            // the test hook: the Advanced tab scrolls to the button, this many frames

static void report_thread(void *arg) {
    (void)arg;
    bool ok = !bug_report_write(g_report.with_save, g_report.info, g_report.path, sizeof g_report.path);
    free(g_report.info);
    g_report.info = nullptr;
    if (ok && g_report.open_folder) {
        char dir[sizeof g_report.path];
        snprintf(dir, sizeof dir, "%s", g_report.path);
        if (char *s = plat_last_sep(dir)) *s = 0;
        plat_open_folder(dir);
    }
    g_report.state.store(ok ? REPORT_DONE : REPORT_FAILED);
}

static void report_start(bool with_save, bool open_folder) {
    if (g_report.state.load() == REPORT_BUSY) return;
    g_report.with_save = with_save;
    g_report.open_folder = open_folder;
    g_report.info = (char *)malloc(16384);
    if (g_report.info && sysinfo_text(g_report.info, 16384) <= 0) g_report.info[0] = 0;
    g_report.state.store(REPORT_BUSY);
    if (!plat_thread_spawn(report_thread, nullptr, "bug_report", 0)) {
        free(g_report.info);
        g_report.info = nullptr;
        g_report.state.store(REPORT_FAILED);
    }
}

static void report_rows(void) {
    int st = g_report.state.load();
    if (g_report_focus) {   // a few frames: on the page's first frame ImGui does not know its height yet
        opt_focus_key("##reset");   // the page's last row: the button and the path above it show too
        g_report_focus--;
    }
    if (opt_button("##bug_report_save", UI_MENU_BUG_REPORT_SAVE, UI_MENU_BUG_REPORT_SAVE_DESC,
                   menu_text(g_report_save ? UI_MENU_ON : UI_MENU_OFF)))
        g_report_save = !g_report_save;
    const char *value = st == REPORT_BUSY ? menu_text(UI_MENU_BUG_REPORT_BUSY)
                      : st == REPORT_FAILED ? menu_text(UI_MENU_ACTION_FAILED) : "";
    if (opt_button("##bug_report", UI_MENU_BUG_REPORT, UI_MENU_BUG_REPORT_DESC, value)) report_start(g_report_save, true);
    if (st == REPORT_DONE) {
        char line[sizeof g_report.path + 64];
        snprintf(line, sizeof line, menu_text(UI_MENU_BUG_REPORT_DONE), g_report.path);
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("%s", line);
        ImGui::PopTextWrapPos();
    }
}

static void tab_advanced(float scale) {
    static ActionNote copy_note, log_note;
    opt_begin("##advanced", scale);
    opt_text(UI_MENU_ADV_WARNING, true);

    opt_group(UI_MENU_GROUP_SCHED);
    static const UiTextId preempt[] = {UI_MENU_PREEMPT_OFF, UI_MENU_PREEMPT_ON, UI_MENU_PREEMPT_INHERIT};   // 0|1|2
    opt_choice("XCX_ASYNC_PREEMPT", UI_MENU_ASYNC_PREEMPT, preempt, 3, UI_MENU_ASYNC_PREEMPT_DESC);
    opt_toggle("XCX_REPLAY_ASYNC", UI_MENU_REPLAY_ASYNC, UI_MENU_REPLAY_ASYNC_DESC);
    opt_toggle("XCX_FREE_CORES", UI_MENU_FREE_CORES, UI_MENU_FREE_CORES_DESC);
    opt_toggle("XCX_TIMER_1MS", UI_MENU_TIMER_1MS, UI_MENU_TIMER_1MS_DESC);
    opt_toggle("XCX_FAST_CORES", UI_MENU_FAST_CORES, UI_MENU_FAST_CORES_DESC);

    opt_group(UI_MENU_GROUP_SHADERS);
    static const UiTextId wait[] = {UI_MENU_PIPE_WAIT_DEFAULT, UI_MENU_PIPE_WAIT_DRAW};
    opt_choice("XCX_PIPE_WAIT", UI_MENU_PIPE_WAIT, wait, 2, UI_MENU_PIPE_WAIT_DESC);
    static const UiTextId skip[] = {UI_MENU_PIPE_SKIP_STRICT, UI_MENU_PIPE_SKIP_ANY, UI_MENU_PIPE_SKIP_FULLSCREEN};
    opt_choice("XCX_PIPE_SKIP", UI_MENU_PIPE_SKIP, skip, 3, UI_MENU_PIPE_SKIP_DESC);
    opt_toggle("XCX_LIB_PREBUILD", UI_MENU_LIB_PREBUILD, UI_MENU_LIB_PREBUILD_DESC);
    row_threads("XCX_LIB_PREBUILD_THREADS", UI_MENU_LIB_PREBUILD_THREADS, UI_MENU_LIB_PREBUILD_THREADS_DESC);
    opt_toggle("XCX_PIPE_PREBUILD", UI_MENU_PIPE_PREBUILD, UI_MENU_PIPE_PREBUILD_DESC);
    row_threads("XCX_PREWARM_XLATE", UI_MENU_PREWARM_XLATE, UI_MENU_PREWARM_XLATE_DESC);
    opt_toggle("XCX_GPL", UI_MENU_GPL, UI_MENU_GPL_DESC);

    opt_group(UI_MENU_GROUP_TEXMEM);
    static const UiTextId astc[] = {UI_MENU_ASTC_AUTO, UI_MENU_ASTC_GPU, UI_MENU_ASTC_NATIVE, UI_MENU_ASTC_CPU};
    opt_choice("XCX_ASTC", UI_MENU_ASTC, astc, 4, UI_MENU_ASTC_DESC);
    static const UiTextId mem[] = {UI_MENU_MEMORY_AUTO, UI_MENU_MEMORY_IMPORT, UI_MENU_MEMORY_MIRROR,
                                   UI_MENU_MEMORY_VERIFY};
    opt_choice("XCX_MEMORY", UI_MENU_MEMORY, mem, 4, UI_MENU_MEMORY_DESC);
    opt_toggle("XCX_TEX_ALIAS", UI_MENU_TEX_ALIAS, UI_MENU_TEX_ALIAS_DESC);

    opt_group(UI_MENU_GROUP_FIXES);
    opt_toggle("XCX_LOD_GUARD", UI_MENU_LOD_GUARD, UI_MENU_LOD_GUARD_DESC);
    opt_toggle("XCX_FIX_C0", UI_MENU_FIX_C0, UI_MENU_FIX_C0_DESC);
    opt_toggle("XCX_FIX_ZERO", UI_MENU_FIX_ZERO, UI_MENU_FIX_ZERO_DESC);
    opt_toggle("XCX_BIND_SKIP", UI_MENU_BIND_SKIP, UI_MENU_BIND_SKIP_DESC);
    opt_toggle("XCX_SHADER_MEMO", UI_MENU_SHADER_MEMO, UI_MENU_SHADER_MEMO_DESC);
    opt_toggle("XCX_PIPE_MEMO", UI_MENU_PIPE_MEMO, UI_MENU_PIPE_MEMO_DESC);
    opt_toggle("XCX_TEX_MEMO", UI_MENU_TEX_MEMO, UI_MENU_TEX_MEMO_DESC);
    opt_toggle("XCX_DESC_TEMPLATE", UI_MENU_DESC_TEMPLATE, UI_MENU_DESC_TEMPLATE_DESC);
    opt_toggle("XCX_PUSH_SKIP", UI_MENU_PUSH_SKIP, UI_MENU_PUSH_SKIP_DESC);
    opt_slider("XCX_SPLIT_DRAWS", UI_MENU_SPLIT_DRAWS, 0, 1000, UI_MENU_SPLIT_DRAWS_DESC, 8, "%.0f");

    opt_group(UI_MENU_GROUP_VULKAN);
    if (!validation_layer_present() && !cfg_bool("XCX_VK_VALIDATION")) opt_disable_next(UI_MENU_VK_VALIDATION_MISSING);
    opt_toggle("XCX_VK_VALIDATION", UI_MENU_VK_VALIDATION, UI_MENU_VK_VALIDATION_DESC);
    static const UiTextId dyn[] = {UI_MENU_AUTO, UI_MENU_OFF, UI_MENU_ON};   // unset, 0, 1
    opt_choice("XCX_VK_DYN_VERTEX", UI_MENU_VK_DYN_VERTEX, dyn, 3, UI_MENU_VK_DYN_VERTEX_DESC);
    opt_choice("XCX_VK_DYN_A2C", UI_MENU_VK_DYN_A2C, dyn, 3, UI_MENU_VK_DYN_A2C_DESC);
    opt_toggle("XCX_VK_SUBGROUP_PIN", UI_MENU_VK_SUBGROUP_PIN, UI_MENU_VK_SUBGROUP_PIN_DESC);

    opt_group(UI_MENU_GROUP_DIAG);
    row_set_or_not("XCX_TRACE", UI_MENU_TRACE, UI_MENU_TRACE_DESC, "perf");
    opt_toggle("XCX_FLIGHT", UI_MENU_FLIGHT, UI_MENU_FLIGHT_DESC);
    opt_slider("XCX_FLIGHT_SPIKE_MS", UI_MENU_FLIGHT_SPIKE_MS, 20, 1000, UI_MENU_FLIGHT_SPIKE_MS_DESC, 10, "%.0f");
    static char rec_path[600];   // the recording goes beside the log
    if (!rec_path[0]) {
        char dir[512];
        snprintf(rec_path, sizeof rec_path, "%s/input-record.txt", sysinfo_log_folder(dir, sizeof dir) ? dir : ".");
    }
    row_set_or_not("XCX_INPUT_RECORD", UI_MENU_INPUT_RECORD, UI_MENU_INPUT_RECORD_DESC, rec_path);
    if (opt_button("##open_log", UI_MENU_OPEN_LOG, UI_MENU_OPEN_LOG_DESC, action_value(log_note, UI_MENU_OPEN_LOG))) {
        char dir[512];
        bool ok = sysinfo_log_folder(dir, sizeof dir) && plat_open_folder(dir);
        if (!ok) action_done(log_note, false);   // success shows as the opened folder
    }
    if (opt_button("##copy_info", UI_MENU_COPY_INFO, UI_MENU_COPY_INFO_DESC,
                   action_value(copy_note, UI_MENU_COPY_INFO_DONE))) {
        static char text[16384];
        action_done(copy_note, sysinfo_text(text, sizeof text) > 0 && plat_clipboard_set(text));
    }
    report_rows();
    bool ask = opt_button("##reset", UI_MENU_RESET, UI_MENU_RESET_DESC);
    opt_end();
    if (ask) ImGui::OpenPopup("##reset_confirm");   // at the level BeginPopupModal looks it up

    // the confirmation, over the menu; Cancel has the cursor first
    g_modal = false;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("##reset_confirm", nullptr,
                               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize)) {
        g_modal = true;
        ImGui::TextUnformatted(menu_text(UI_MENU_RESET_CONFIRM));
        ImGui::Spacing();
        const char *yes = menu_text(UI_MENU_RESET_YES), *no = menu_text(UI_MENU_RESET_NO);
        const ImGuiStyle &st = ImGui::GetStyle();
        float bw = fmaxf(ImGui::CalcTextSize(yes).x, ImGui::CalcTextSize(no).x) + st.FramePadding.x * 4;
        bool close = false;
        if (ImGui::Button(no, ImVec2(bw, 0))) close = true;
        if (ImGui::IsWindowAppearing()) ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button(yes, ImVec2(bw, 0))) {
            reset_all();
            close = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))
            close = true;
        if (close) {
            ImGui::CloseCurrentPopup();
            opt_focus_key("##reset");   // back on the row it came from
        }
        ImGui::EndPopup();
    }
}

static const UiTextId kTabNames[TAB_COUNT] = {UI_MENU_TAB_SCREEN, UI_MENU_TAB_GAME, UI_MENU_TAB_SOUND,
                                              UI_MENU_TAB_CONTROLS, UI_MENU_TAB_MENU, UI_MENU_TAB_ADVANCED, UI_MENU_TAB_MODS,
                                              UI_MENU_TAB_CHEATS};
static int g_tab;               // the tab shown
static uint64_t g_registered_epoch;
static char g_registered_owner[65];
static int g_tab_request = -1;  // select this tab at the next layout; -1 none
static int g_focus_page = -1;   // this tab's first row takes the cursor once it is shown; -1 none

// ---- the text entry (swkbd_sdl.c): a field and controller keyboard over the last game image. A activates a grid key;
// Plus or the OK cell and Enter confirm, B or the Cancel cell and Esc cancel; the gamepad's buttons count once seen released, so the press that
// opened the entry does not answer it. The fields below belong to the present path while g_text is set; text_lock
// orders menu_text_poll's read of the answer after the frame that wrote it.
static PlatMutex text_lock = PLAT_MUTEX_INIT;
static char g_text_buf[(KB_LIMIT + 1) * 3], g_text_prompt[0x101 * 3];
static int g_text_max, g_text_min, g_text_digits, g_text_focus, g_text_answer, g_text_frames;
static uint32_t g_text_disabled;
static int g_text_row, g_text_col, g_text_upper, g_text_shift, g_text_once, g_text_edit, g_text_select_initial;
static int g_text_pad_ready, g_text_repeat_dir = -1;
static uint64_t g_text_repeat_at;
static const int text_columns[] = {11, 10, 10, 8, 1, 2};
static const char *const text_letters[] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"};
enum { TEXT_CASE = -1, TEXT_BACKSPACE = -2, TEXT_OK = -3, TEXT_CANCEL = -4, TEXT_DELETE = -5, TEXT_LEFT = -6, TEXT_RIGHT = -7, TEXT_SHIFT = -8 };

static void registered_route(bool &open,bool text){
    if(g_registered_epoch){ModMenuEntry current;if(!open){g_registered_epoch=0;}else if(text||g_failed_flag()||mod_menu_epoch()!=g_registered_epoch||!mod_menu_snapshot(&current)||strcmp(current.owner,g_registered_owner)){set_open(0);open=false;g_registered_epoch=0;}}
    ModMenuEntry requested;if(mod_menu_take(&requested)&&!open&&!text&&!g_failed_flag()){
        mods_page_select(requested.owner,requested.page);g_tab_request=TAB_MODS;g_registered_epoch=requested.epoch;snprintf(g_registered_owner,sizeof g_registered_owner,"%s",requested.owner);set_open(1);open=true;
    }

}

extern "C" void menu_text_begin(const char *prompt, const char *initial, int max_len, int min_len, uint32_t disabled,
                                int digits_only) {
    mod_ui_interaction(0);
    plat_mutex_lock(&text_lock);
    snprintf(g_text_prompt, sizeof g_text_prompt, "%s", prompt);
    snprintf(g_text_buf, sizeof g_text_buf, "%s", initial);
    g_text_max = max_len, g_text_min = min_len, g_text_disabled = disabled, g_text_digits = digits_only;
    g_text_focus = 1, g_text_answer = g_text_frames = 0;
    g_text_row = 1, g_text_col = g_text_upper = g_text_shift = g_text_once = g_text_edit = 0;
    g_text_pad_ready = 0;
    g_text_select_initial = 1;
    g_text_repeat_dir = -1;
    g_reset.store(1);
    g_text.store(1);
    plat_mutex_unlock(&text_lock);
}

extern "C" int menu_text_poll(char *out, size_t cap) {
    plat_mutex_lock(&text_lock);
    int a = g_text_answer;
    if (a) {
        snprintf(out, cap, "%s", g_text_buf);
        g_text.store(0);
        g_reset.store(1);
    }
    plat_mutex_unlock(&text_lock);
    return a;
}

// UTF-16 units of UTF-8 text, as the applet counts the length. With cut: *cut is the byte offset of the character that
// takes the count past max, -1 when none does.
static int text_units(const char *s, int max, int *cut) {
    int units = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p;) {
        int len = *p < 0x80 ? 1 : *p < 0xe0 ? 2 : *p < 0xf0 ? 3 : 4;
        units += len == 4 ? 2 : 1;
        if (cut && units > max) {
            *cut = (int)((const char *)p - s);
            return units;
        }
        while (len-- && *p) p++;
    }
    if (cut) *cut = -1;
    return units;
}

static int text_callback(ImGuiInputTextCallbackData *d) {
    if (d->EventFlag == ImGuiInputTextFlags_CallbackCharFilter)
        return d->EventChar > 0xffff || !swkbd_allowed((uint16_t)d->EventChar, g_text_disabled, g_text_digits);
    g_text_select_initial = 0;
    if (int edit = g_text_edit) {
        g_text_edit = 0;
        bool movement = edit == TEXT_LEFT || edit == TEXT_RIGHT;
        bool deletion = edit == TEXT_BACKSPACE || edit == TEXT_DELETE;
        bool allowed = movement || deletion || swkbd_allowed((uint16_t)edit, g_text_disabled, g_text_digits);
        if (allowed) {
            int start = ImMin(d->SelectionStart, d->SelectionEnd);
            int end = ImMax(d->SelectionStart, d->SelectionEnd);
            if (movement) {
                int pos = start != end ? (edit == TEXT_LEFT ? start : end) : d->CursorPos;
                if (start == end && edit == TEXT_LEFT && pos) {
                    --pos;
                    while (pos && ((unsigned char)d->Buf[pos] & 0xc0) == 0x80) --pos;
                } else if (start == end && edit == TEXT_RIGHT && pos < d->BufTextLen) {
                    ++pos;
                    while (pos < d->BufTextLen && ((unsigned char)d->Buf[pos] & 0xc0) == 0x80) ++pos;
                }
                d->CursorPos = d->SelectionStart = d->SelectionEnd = pos;
            } else {
                if (edit == TEXT_BACKSPACE && start == end) {
                    end = d->CursorPos;
                    start = end;
                    if (start) {
                        --start;
                        while (start && ((unsigned char)d->Buf[start] & 0xc0) == 0x80) --start;
                    }
                }
                if (edit == TEXT_DELETE && start == end) {
                    start = end = d->CursorPos;
                    if (end < d->BufTextLen) {
                        ++end;
                        while (end < d->BufTextLen && ((unsigned char)d->Buf[end] & 0xc0) == 0x80) ++end;
                    }
                }
                if (end > start) d->DeleteChars(start, end - start);
                if (!deletion) {
                    char c = (char)edit;
                    d->InsertChars(d->CursorPos, &c, &c + 1);
                    g_text_once = 0;
                }
            }
        }
    }
    int cut;
    if (g_text_max > 0) {
        text_units(d->Buf, g_text_max, &cut);
        if (cut >= 0) d->DeleteChars(cut, d->BufTextLen - cut);
    }
    return 0;
}

static float text_key_units(int row, int col) {
    if (row == 0 && col == 10) return 2;
    if (row == 2 && col == 0) return 1.5f;
    if (row == 3 && col == 0) return 2;
    if (row == 4) return 6;
    if (row == 5) return 2;
    return 1;
}

static const float text_indent[] = {0, 0.5f, 0, 0, 3, 8};
static float text_key_center(int row, int col) {
    float x = text_indent[row];
    for (int i = 0; i < col; i++) x += text_key_units(row, i);
    return x + text_key_units(row, col) * 0.5f;
}

static int text_key(int row, int col) {
    if (row == 0 && col == 10) return TEXT_BACKSPACE;
    if (row == 2 && col == 0) return TEXT_CASE;
    if (row == 3 && col == 0) return TEXT_SHIFT;
    if (row == 4) return ' ';
    if (row == 5) return col == 0 ? TEXT_OK : TEXT_CANCEL;
    int c = text_letters[row][col - (row == 2 || row == 3)];
    return (g_text_upper != (g_text_shift || g_text_once)) && c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
}

static int text_activate(int key) {
    if (key == TEXT_OK) return 1;
    if (key == TEXT_CANCEL) return -1;
    if (key == TEXT_CASE) g_text_upper = !g_text_upper;
    else if (key == TEXT_SHIFT) g_text_once = !g_text_once;
    else g_text_edit = key;
    g_text_focus = 2;   // return from a mouse-operated grid cell without selecting the whole name again
    return 0;
}

// Grid navigation is separate from the field's cursor. Only directions repeat; an activation needs a new press.
static int text_pad(int reset) {
    uint64_t b;
    int32_t lx, ly;
    int have = input_pad_read(&b, &lx, &ly);
    if (reset || !have || !ikb_foreground()) {
        g_text_pad_ready = g_text_shift = 0;
        g_text_repeat_dir = stick_dir = -1;
        pad_prev = b;
        return 0;
    }
    int dir = stick_to_dpad(lx, ly);
    if (dir >= 0) b |= 1ull << dir;
    if (!g_text_pad_ready) {
        pad_prev = b;
        if (!b) g_text_pad_ready = 1;
        return 0;
    }
    g_text_shift = !!(b & 1ull << B_ZR);
    uint64_t down = b & ~pad_prev;
    pad_prev = b;
    if (down & 1ull << B_B) return -1;
    if (down & 1ull << B_PLUS) return 1;
    if (down & 1ull << B_X) return text_activate(TEXT_BACKSPACE);
    if (down & 1ull << B_Y) return text_activate(TEXT_DELETE);
    if (down & 1ull << B_ZL) return text_activate(TEXT_CASE);
    if (down & 1ull << B_L) return text_activate(TEXT_LEFT);
    if (down & 1ull << B_R) return text_activate(TEXT_RIGHT);
    int held = b & 1ull << B_LEFT ? B_LEFT : b & 1ull << B_RIGHT ? B_RIGHT
             : b & 1ull << B_UP ? B_UP : b & 1ull << B_DOWN ? B_DOWN : -1;
    uint64_t now = plat_now_ns();
    bool step = held >= 0 && (held != g_text_repeat_dir || now >= g_text_repeat_at);
    if (step) {
        g_text_repeat_at = now + (held != g_text_repeat_dir ? 350000000ull : 90000000ull);
        if (held == B_LEFT) g_text_col = (g_text_col + text_columns[g_text_row] - 1) % text_columns[g_text_row];
        if (held == B_RIGHT) g_text_col = (g_text_col + 1) % text_columns[g_text_row];
        if (held == B_UP || held == B_DOWN) {
            float x = text_key_center(g_text_row, g_text_col);
            g_text_row = (g_text_row + (held == B_UP ? 5 : 1)) % 6;
            g_text_col = 0;
            for (int col = 1; col < text_columns[g_text_row]; col++)
                if (fabsf(text_key_center(g_text_row, col) - x) < fabsf(text_key_center(g_text_row, g_text_col) - x))
                    g_text_col = col;
        }
    }
    g_text_repeat_dir = held;
    return down & 1ull << B_A ? text_activate(text_key(g_text_row, g_text_col)) : 0;
}

static int text_grid(float scale, bool short_text) {
    ImGui::Spacing();
    float width = ImGui::GetContentRegionAvail().x;
    const float gap = ImGui::GetStyle().ItemSpacing.x * 0.4f;
    int answer = 0;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2 * scale, 8 * scale));
    for (int row = 0; row < 6; row++) {
        float cell = (width - gap * 11) / 12;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + text_indent[row] * (cell + gap));
        for (int col = 0; col < text_columns[row]; col++) {
            if (col) ImGui::SameLine();
            int key = text_key(row, col);
            char letter[] = {(char)key, 0};
            const char *label = key == TEXT_CASE ? "##caps"
                              : key == TEXT_SHIFT ? "##shift"
                              : key == ' ' ? ui_text(UI_SWKBD_SPACE)
                              : key == TEXT_BACKSPACE ? ui_text(UI_SWKBD_BACKSPACE)
                              : key == TEXT_OK ? ui_text(UI_SWKBD_OK)
                              : key == TEXT_CANCEL ? ui_text(UI_SWKBD_CANCEL) : letter;
            bool selected = row == g_text_row && col == g_text_col;
            bool disabled = key == TEXT_OK ? short_text : key > 0 && !swkbd_allowed((uint16_t)key, g_text_disabled, g_text_digits);
            float units = text_key_units(row, col);
            float key_width = units * cell + (units - 1) * gap;
            bool lit = key == TEXT_CASE ? g_text_upper : key == TEXT_SHIFT && (g_text_shift || g_text_once);
            ImGui::PushID(row * 16 + col);
            if (selected || lit) ImGui::PushStyleColor(ImGuiCol_Button, lit ? ImVec4(0.08f, 0.58f, 0.42f, 1) : ImVec4(0.08f, 0.48f, 0.59f, 1));
            ImGui::BeginDisabled(disabled);
            if (ImGui::ButtonEx(label, ImVec2(key_width, 46 * scale), ImGuiButtonFlags_NoNavFocus)) {
                g_text_row = row; g_text_col = col;
                answer = text_activate(key);
            }
            ImGui::EndDisabled();
            if (key == TEXT_CASE || key == TEXT_SHIFT) {
                ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
                float x = (min.x + max.x) * 0.5f, y = (min.y + max.y) * 0.5f - 2 * scale;
                float u = 9 * scale;
                ImVec2 arrow[] = {{x, y-u}, {x+u, y}, {x+u*0.4f, y}, {x+u*0.4f, y+u*0.7f},
                                  {x-u*0.4f, y+u*0.7f}, {x-u*0.4f, y}, {x-u, y}};
                ImDrawList *draw = ImGui::GetWindowDrawList();
                ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
                draw->AddPolyline(arrow, IM_ARRAYSIZE(arrow), color, ImDrawFlags_Closed, 2 * scale);
                if (key == TEXT_CASE) draw->AddLine(ImVec2(x-u*0.4f, y+u), ImVec2(x+u*0.4f, y+u), color, 2 * scale);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ui_text(key == TEXT_CASE ? UI_SWKBD_SHIFT : UI_SWKBD_MOMENTARY_SHIFT));
            }
            if (selected) ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                IM_COL32(97, 222, 245, 255), ImGui::GetStyle().FrameRounding, 0, 2 * scale);
            if (selected || lit) ImGui::PopStyleColor();
            ImGui::PopID();
        }
    }
    ImGui::PopStyleVar(2);
    return answer;
}

// Test hook XCX_SWKBD_TEST=ok|cancel|type:<text>: OK or Cancel 30 frames into the entry; type: types <text> at frame
// 20 (over the selected starting text) and presses OK at frame 40. The frame that answers goes to XCX_MENU_TEST_DIR as
// swkbd-test-<frame>.png. Returns the answer as text_pad does.
static int text_test(ImGuiIO &io) {
    const char *t = cfg_get("XCX_SWKBD_TEST");
    if (!t) return 0;
    int f = ++g_text_frames;
    if (!strncmp(t, "type:", 5)) {
        if (f == 20) io.AddInputCharactersUTF8(t + 5);
        return f >= 40;
    }
    return f < 30 ? 0 : !strcmp(t, "cancel") ? -1 : 1;
}

// The text entry's window; returns this frame's answer (1 OK, -1 Cancel, 0 none). pad: text_pad's answer.
static int layout_text(uint32_t w, uint32_t h, float scale, int pad) {
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2((float)w, (float)h), IM_COL32(0, 0, 0, 120));
    ImGui::SetNextWindowPos(ImVec2(w * 0.5f, h * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(fminf(w * 0.9f, 1040 * scale), 0), ImGuiCond_Always);
    ImGui::Begin("##text", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
    ImGui::TextColored(ImVec4(0.35f, 0.86f, 0.96f, 1), "%s", ui_text(UI_SWKBD_TITLE));
    ImGui::Spacing();
    ImGui::TextWrapped("%s", g_text_prompt[0] ? g_text_prompt : ui_text(UI_SWKBD_PROMPT));
    int answer = ImGui::IsKeyPressed(ImGuiKey_Escape, false) ? -1 : pad;
    ImGui::SetNextItemWidth(-FLT_MIN);
    // the field keeps the keyboard: at the start, its text selected so typing replaces it, and after a click beside it
    if (g_text_focus == 1) {
        // The widget id is reused across requests; never apply the previous request's deactivated buffer.
        GImGui->InputTextDeactivatedState.ID = 0;
        if (ImGuiInputTextState *state = ImGui::GetInputTextState(ImGui::GetID("##text_field")))
            state->ReloadUserBufAndSelectAll();
    }
    bool select_initial = g_text_select_initial != 0;
    if (g_text_focus == 1 || (ImGui::GetActiveID() != ImGui::GetID("##text_field") &&
                           (g_text_focus || (!ImGui::GetActiveID() && !ImGui::IsMouseDown(0)))))
        ImGui::SetKeyboardFocusHere();
    g_text_focus = 0;
    if (ImGui::InputText("##text_field", g_text_buf, sizeof g_text_buf,
                         ImGuiInputTextFlags_EnterReturnsTrue | (select_initial ? ImGuiInputTextFlags_AutoSelectAll : 0) |
                             ImGuiInputTextFlags_CallbackCharFilter | ImGuiInputTextFlags_CallbackEdit | ImGuiInputTextFlags_CallbackAlways,
                         text_callback) && !answer)
        answer = 1;
    int short_text = text_units(g_text_buf, 0, nullptr) < g_text_min;
    if (int a = text_grid(scale, short_text)) answer = a;
    ImGui::End();
    return answer == 1 && short_text ? 0 : answer;
}

// ---- the window: title, tabs (L / R or Q / E switch), the page, and the bottom row with "Continue" and "Quit game".
// B on the gamepad and Esc close it.
static void layout_menu(uint32_t w, uint32_t h, float scale) {
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2((float)w, (float)h), IM_COL32(0, 0, 0, 120));
    float ww = fminf(w * 0.88f, 1400 * scale), wh = fminf(h * 0.86f, 820 * scale);
    ImGui::SetNextWindowPos(ImVec2(w * 0.5f, h * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ww, wh), ImGuiCond_Always);
    ImGui::Begin("##menu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextColored(ImVec4(0.35f, 0.86f, 0.96f, 1), "%s", menu_text(UI_MENU_TITLE));
    // the port version and the game update, right-aligned on the title line: what a bug report needs first
    char game[16], version[96];
    if (*rt_game_name) snprintf(game, sizeof game, "%s", rt_game_name);
    else snprintf(game, sizeof game, "%d.%d.%d", rt_game_version / 100, rt_game_version / 10 % 10, rt_game_version % 10);
    snprintf(version, sizeof version, menu_text(UI_MENU_VERSION), rt_port_version, game);
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - ImGui::CalcTextSize(version).x);
    ImGui::TextDisabled("%s", version);

    bool popup_was_open=ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId);
    int step = g_modal || binds_modal() || popup_was_open                                                                  ? 0
             : ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false) || ImGui::IsKeyPressed(ImGuiKey_E, false) ? 1
             : ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false) || ImGui::IsKeyPressed(ImGuiKey_Q, false) ? -1
                                                                                                         : 0;
    int tabs = cfg_bool("XCX_CHEATS") ? TAB_COUNT : TAB_CHEATS;   // the Cheats tab is last and shows while cheats are on
    if (g_tab >= tabs) g_tab_request = TAB_GAME;   // cheats turned off on their own tab: back to the switch
    if (step) g_tab_request = (g_tab + step + tabs) % tabs;
    if (g_tab_request >= 0) g_focus_page = g_tab_request;   // the new page's first row takes the cursor

    const ImGuiStyle &st = ImGui::GetStyle();
    float bottom = ImGui::GetFrameHeight() + st.ItemSpacing.y * 2;
    if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_NoTabListScrollingButtons)) {
        for (int k = 0; k < tabs; k++) {
            char label[128];
            snprintf(label, sizeof label, "  %s  ###tab%d", menu_text(kTabNames[k]), k);   // the id stays per language
            if (!ImGui::BeginTabItem(label, nullptr, g_tab_request == k ? ImGuiTabItemFlags_SetSelected : 0)) continue;
            g_tab = k;
            bool focus_page = g_focus_page == k;
            if (focus_page) {
                opt_focus_first();
                g_focus_page = -1;
            }
            ImGui::Spacing();
            // the page's rows are a NavFlattened child of this window itself: ImGui clips the items of a direct child
            // only, so behind a second child level the rows scrolled out above the tabs won D-pad moves from the
            // buttons and the tabs (up from "Continue" went to the tabs, down from the tabs to "Continue")
            opt_reserve_bottom(bottom);
            switch (k) {
            case TAB_SCREEN: tab_screen(scale); break;
            case TAB_GAME: tab_game(scale); break;
            case TAB_SOUND: tab_sound(scale); break;
            case TAB_CONTROLS: tab_controls(scale); break;
            case TAB_MENU: tab_menu(scale); break;
            case TAB_ADVANCED: tab_advanced(scale); break;
            case TAB_MODS: mods_page(scale,bottom,focus_page); break;
            case TAB_CHEATS: tab_cheats(scale); break;
            }
            ImGui::EndTabItem();
        }
        g_tab_request = -1;
        ImGui::EndTabBar();
    }

    if(g_tab!=TAB_MODS)mod_ui_interaction(0);
    // bottom row: the save status on the left, the two buttons on the right
    ImGui::Spacing();
    float y = ImGui::GetCursorPosY();
    if (!cfg_last_save_ok()) {   // stays until a save succeeds; the settings hold for the session
        ImGui::SetCursorPosY(y + st.FramePadding.y);
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1), "%s", menu_text(UI_MENU_SAVE_FAILED));
    }
    const char *cont = menu_text(UI_MENU_CONTINUE), *quit = menu_text(UI_MENU_QUIT);
    float bw = fmaxf(ImGui::CalcTextSize(cont).x, ImGui::CalcTextSize(quit).x) + st.FramePadding.x * 4;
    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - st.WindowPadding.x - bw * 2 - st.ItemSpacing.x, y));
    if (ImGui::Button(cont, ImVec2(bw, 0))) set_open(0);
    ImGui::SameLine();
    if (ImGui::Button(quit, ImVec2(bw, 0))) gfx_window_request_close();
    bool was_modal = g_modal || binds_modal() || popup_was_open;
    if (g_tab != TAB_ADVANCED) g_modal = false;
    if (!was_modal && (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)))
        set_open(0);
    ImGui::End();
}

// The Quit Game prompt over the dimmed game: No has the cursor first, and B or Esc answer No as well. Yes ends the
// process as the window's close button does (gfx_window_closed): nothing is saved, a save already being written
// finishes first.
static void layout_quit(uint32_t w, uint32_t h) {
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2((float)w, (float)h), IM_COL32(0, 0, 0, 120));
    ImGui::SetNextWindowPos(ImVec2(w * 0.5f, h * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowFocus();
    ImGui::Begin("##quit", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextUnformatted(menu_text(UI_QUIT_CONFIRM));
    ImGui::Spacing();
    const char *yes = menu_text(UI_QUIT_YES), *no = menu_text(UI_QUIT_NO);
    const ImGuiStyle &st = ImGui::GetStyle();
    float bw = fmaxf(ImGui::CalcTextSize(yes).x, ImGui::CalcTextSize(no).x) + st.FramePadding.x * 8;
    bool back = ImGui::Button(no, ImVec2(bw, 0));
    if (ImGui::IsWindowAppearing()) ImGui::SetItemDefaultFocus();
    ImGui::SameLine();
    if (ImGui::Button(yes, ImVec2(bw, 0))) {
        rt_log("menu: quit prompt: yes");
        gfx_window_request_close();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) back = true;
    if (back && g_quit.exchange(0) == 1) set_open(0);
    ImGui::End();
}

static int g_save_pending;   // a changed setting still waits for its write to xcx.ini (opt_save_tick)

// ---- the performance overlay (XCX_PERF_OVERLAY), in the top right corner. "fps": the frame rate over the presents of
// the last half second. "full": a panel of perfmon.c's numbers, in the style of SavageWindRecomp's perf_overlay_draw
// (menu.mm): the machine's load and temperatures, the frame rate and frame times with a graph, the render thread's waits.
// Off, it costs a cfg_version load per present.
static unsigned g_perf_ver = ~0u;
static int g_perf_mode;                  // 0 off, 1 fps, 2 full
static std::atomic<int> g_perf_save;     // F10 changed the mode: xcx.ini is written on the present thread
static uint64_t g_perf_last_ns, g_perf_win_ns, g_perf_max_ns;
static int g_perf_frames;
static char g_perf_text[96];

static int perf_mode_of(const char *s) { return !strcmp(s, "full") ? 2 : !strcmp(s, "fps") ? 1 : 0; }

extern "C" void menu_perf_cycle(void) {
    static const char *const modes[] = {"off", "fps", "full"};
    cfg_set("XCX_PERF_OVERLAY", modes[(perf_mode_of(cfg_value("XCX_PERF_OVERLAY")) + 1) % 3]);
    g_perf_save.store(1);
}

// The device the game renders on, for perfmon's GPU counters (by LUID), NVML (by name) and ADL (by PCI bus, else
// name); once, before full mode.
static void perf_gpu_once(const MenuTarget *t) {
    static bool done;
    if (done) return;
    done = true;
    uint32_t ne = 0;
    vkEnumerateDeviceExtensionProperties(t->phys, NULL, &ne, NULL);
    VkExtensionProperties *ep = (VkExtensionProperties *)calloc(ne ? ne : 1, sizeof *ep);
    bool has_bus = false;
    if (ep && vkEnumerateDeviceExtensionProperties(t->phys, NULL, &ne, ep) >= 0)
        for (uint32_t i = 0; i < ne && !has_bus; i++) has_bus = !strcmp(ep[i].extensionName, VK_EXT_PCI_BUS_INFO_EXTENSION_NAME);
    free(ep);
    VkPhysicalDevicePCIBusInfoPropertiesEXT bus = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT};
    VkPhysicalDeviceIDProperties id = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES, has_bus ? &bus : nullptr};
    VkPhysicalDeviceProperties2 p2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &id};
    vkGetPhysicalDeviceProperties2(t->phys, &p2);
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(t->phys, &mp);
    uint64_t vram = 0;
    for (uint32_t h = 0; h < mp.memoryHeapCount; h++)
        if (mp.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) vram += mp.memoryHeaps[h].size;
    perfmon_set_gpu(p2.properties.deviceName, id.deviceLUIDValid ? id.deviceLUID : NULL, has_bus ? (int)bus.pciBus : -1,
                    (float)(vram >> 20));
}

static void perf_tick(uint64_t now, const MenuTarget *t) {
    if (cfg_version() != g_perf_ver) {
        g_perf_ver = cfg_version();
        int m = perf_mode_of(cfg_value("XCX_PERF_OVERLAY"));
        if (m != g_perf_mode) {
            g_perf_last_ns = 0, g_perf_text[0] = 0;
            if (m == 2) perf_gpu_once(t);
            perfmon_mode(m);
        }
        g_perf_mode = m;
    }
    if (g_perf_mode != 1) return;
    uint64_t d = now - g_perf_last_ns;
    if (!g_perf_last_ns || d > 2000000000ull) {   // first frame, or a stall the window should not average in
        g_perf_win_ns = g_perf_max_ns = 0;
        g_perf_frames = 0;
    } else {
        g_perf_win_ns += d;
        g_perf_frames++;
        if (d > g_perf_max_ns) g_perf_max_ns = d;
        if (g_perf_win_ns >= 500000000ull) {
            snprintf(g_perf_text, sizeof g_perf_text, menu_text(UI_PERF_LINE_FPS), g_perf_frames * 1e9 / g_perf_win_ns);
            g_perf_win_ns = g_perf_max_ns = 0;
            g_perf_frames = 0;
        }
    }
    g_perf_last_ns = now;
}

// The panel keeps its width while the numbers change: every column is as wide as the widest text it can show, measured
// on samples of 8s (Roboto's digits share one width), and the numbers are right-aligned in it.
struct PerfCol { UiTextId fmt; float sample; };

static float perf_text_w(UiTextId fmt, float v) {
    char b[64];
    snprintf(b, sizeof b, menu_text(fmt), v);
    return ImGui::CalcTextSize(b).x;
}

// A fixed column as wide as the widest of the formats (with their samples) and labels given.
static void perf_column(const char *id, const PerfCol *cols, int n, const UiTextId *labels = nullptr, int nl = 0) {
    float w = ImGui::CalcTextSize(menu_text(UI_MENU_OV_NONE)).x;
    for (int i = 0; i < n; i++) w = fmaxf(w, perf_text_w(cols[i].fmt, cols[i].sample));
    for (int i = 0; i < nl; i++) w = fmaxf(w, ImGui::CalcTextSize(menu_text(labels[i])).x);
    ImGui::TableSetupColumn(id, ImGuiTableColumnFlags_WidthFixed, w);
}

static void perf_right(const char *s, bool dim) {
    float pad = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(s).x;
    if (pad > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
    if (dim) ImGui::TextDisabled("%s", s);
    else ImGui::TextUnformatted(s);
}

// One cell of the full panel's table: fmt (a catalog format, one number) with v, or "-" when the machine gives none.
static void perf_cell(UiTextId fmt, float v) {
    ImGui::TableNextColumn();
    if (v < 0) return perf_right(menu_text(UI_MENU_OV_NONE), true);
    char b[64];
    snprintf(b, sizeof b, menu_text(fmt), v);
    perf_right(b, false);
}

static void perf_label(UiTextId label, const ImVec4 &c) {
    ImGui::TableNextColumn();
    ImGui::TextColored(c, "%s", menu_text(label));
}

static float perf_gb(float mb) { return mb < 0 ? -1 : mb / 1024; }

// The picture lines: the window, the game image the present shows and the 3D scene it holds (resolution.c), how the
// present scales the image to the window (gfx_present.c, the last present), the frame limit and the presentation mode
// in use.
static void perf_picture(const MenuTarget *t, const ImVec4 &c) {
    unsigned out[2], scene[2];
    res_sizes(out, scene);
    char v[6][64];
    snprintf(v[0], sizeof v[0], menu_text(UI_MENU_OV_FMT_SIZE), t->width, t->height);
    snprintf(v[1], sizeof v[1], menu_text(UI_MENU_OV_FMT_SIZE), (unsigned)t->image_w, (unsigned)t->image_h);
    snprintf(v[2], sizeof v[2], menu_text(UI_MENU_OV_FMT_SCENE), scene[0], scene[1], scene[0] * 100.0 / out[0]);
    if (t->scaler == 2) {
        int s = cfg_int("XCX_SHARPNESS");
        snprintf(v[3], sizeof v[3], menu_text(UI_MENU_OV_FMT_FSR), s < 0 ? 0 : s > 100 ? 100 : s);
    } else {
        snprintf(v[3], sizeof v[3], "%s", menu_text(t->scaler ? UI_MENU_OV_SCALE_BILINEAR : UI_MENU_OV_SCALE_NONE));
    }
    double hz = frame_step_limit_hz();
    snprintf(v[4], sizeof v[4], "%s", menu_text(UI_MENU_OV_NONE));
    if (hz > 0) snprintf(v[4], sizeof v[4], menu_text(UI_MENU_OV_FPS), hz);
    const char *pm = present_name(gfx_window_present_mode());
    snprintf(v[5], sizeof v[5], "%s", pm ? pm : menu_text(UI_MENU_OV_NONE));
    static const UiTextId labels[6] = {UI_MENU_OV_WINDOW, UI_MENU_OV_OUTPUT, UI_MENU_OV_SCENE,
                                       UI_MENU_OV_SCALING, UI_MENU_OV_LIMIT, UI_MENU_OV_PRESENT};
    if (!ImGui::BeginTable("##perfp", 2, ImGuiTableFlags_SizingFixedFit)) return;
    float vw = 0;
    for (auto &s : v) vw = fmaxf(vw, ImGui::CalcTextSize(s).x);
    ImGui::TableSetupColumn("##p0", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("##p1", ImGuiTableColumnFlags_WidthFixed, vw);
    for (int i = 0; i < 6; i++) {
        perf_label(labels[i], c);
        ImGui::TableNextColumn();
        perf_right(v[i], false);
    }
    ImGui::EndTable();
}

static void perf_full_draw(const MenuTarget *t, float scale) {
    float w = (float)t->width;
    PerfStats s;
    perfmon_snapshot(&s);
    ImGui::SetNextWindowPos(ImVec2(w - 12 * scale, 12 * scale), ImGuiCond_Always, ImVec2(1, 0));
    ImGui::SetNextWindowBgAlpha(0.6f);
    ImGui::Begin("##perf", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                                        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings);
    // under the menu while it is open (the settings stay readable), else over everything
    if (g_open.load(std::memory_order_relaxed)) ImGui::BringWindowToDisplayBack(ImGui::GetCurrentWindow());
    else ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    ImGui::SetWindowFontScale(0.72f);
    const ImVec4 gpu_c(0.55f, 0.83f, 0.31f, 1), cpu_c(0.18f, 0.59f, 0.80f, 1), ram_c(0.80f, 0.45f, 0.70f, 1),
        fps_c(0.95f, 0.95f, 0.95f, 1), wait_c(0.93f, 0.76f, 0.33f, 1), pic_c(0.62f, 0.70f, 0.95f, 1);
    if (ImGui::BeginTable("##perft", 4, ImGuiTableFlags_SizingFixedFit)) {
        static const UiTextId labels[] = {UI_MENU_OV_GPU, UI_MENU_OV_VRAM, UI_MENU_OV_CPU, UI_MENU_OV_RAM};
        static const PerfCol c0[] = {{UI_MENU_OV_FPS, 888}};
        static const PerfCol c1[] = {{UI_MENU_OV_FMT_PCT, 100}, {UI_MENU_OV_FMT_GB, 888.8f}, {UI_MENU_OV_FRAME_MS, 888.8f}};
        static const PerfCol c2[] = {{UI_MENU_OV_FMT_TEMP, 188}, {UI_MENU_OV_FMT_GB_OF, 888.8f}, {UI_MENU_OV_MAX_MS, 8888.8f}};
        static const PerfCol c3[] = {{UI_MENU_OV_FMT_POWER, 888}, {UI_MENU_OV_FMT_MHZ, 8888}, {UI_MENU_OV_GAME_PCT, 100},
                                     {UI_MENU_OV_GAME_GB, 888.8f}};
        perf_column("##c0", c0, 1, labels, 4);
        perf_column("##c1", c1, 3);
        perf_column("##c2", c2, 3);
        perf_column("##c3", c3, 4);
        perf_label(UI_MENU_OV_GPU, gpu_c);
        perf_cell(UI_MENU_OV_FMT_PCT, s.gpu_pct);
        perf_cell(UI_MENU_OV_FMT_TEMP, s.gpu_temp_c);
        perf_cell(UI_MENU_OV_FMT_POWER, s.gpu_power_w);
        perf_label(UI_MENU_OV_VRAM, gpu_c);
        perf_cell(UI_MENU_OV_FMT_GB, perf_gb(s.vram_used_mb));
        perf_cell(UI_MENU_OV_FMT_GB_OF, perf_gb(s.vram_total_mb));
        perf_cell(UI_MENU_OV_FMT_MHZ, s.gpu_clock_mhz);
        perf_label(UI_MENU_OV_CPU, cpu_c);
        perf_cell(UI_MENU_OV_FMT_PCT, s.cpu_total_pct);
        perf_cell(UI_MENU_OV_FMT_TEMP, s.cpu_temp_c);
        perf_cell(UI_MENU_OV_GAME_PCT, s.cpu_game_pct);
        perf_label(UI_MENU_OV_RAM, ram_c);
        perf_cell(UI_MENU_OV_FMT_GB, perf_gb(s.ram_used_mb));
        perf_cell(UI_MENU_OV_FMT_GB_OF, perf_gb(s.ram_total_mb));
        perf_cell(UI_MENU_OV_GAME_GB, perf_gb(s.ram_game_mb));
        ImGui::TableNextColumn();   // the frame row: no label, the rate in its place
        char b[64];
        snprintf(b, sizeof b, menu_text(UI_MENU_OV_FPS), s.fps);
        ImGui::TextColored(fps_c, "%s", b);
        perf_cell(UI_MENU_OV_FRAME_MS, s.frame_ms_avg);
        perf_cell(UI_MENU_OV_MAX_MS, s.frame_ms_max);
        ImGui::EndTable();
    }
    float top = s.frame_ms_max > 50 ? s.frame_ms_max : 50;
    // as wide as the panel the tables size; the plot does not count toward that width, so the panel can still shrink
    ImGuiWindow *win = ImGui::GetCurrentWindow();
    float max_x = win->DC.CursorMaxPos.x;
    ImGui::PlotLines("##ft", s.frame_ms, PERF_HISTORY, 0, nullptr, 0, top, ImVec2(-FLT_MIN, 48 * scale));
    win->DC.CursorMaxPos.x = max_x;
    // per frame, averaged over the last half second; the labels' column stretches, so the numbers end at the panel's
    // right edge, and the table still asks for no more than its widest label and number
    if (ImGui::BeginTable("##perfw", 2, ImGuiTableFlags_SizingFixedFit)) {
        static const PerfCol c1[] = {{UI_MENU_OV_FMT_MS, 888.88f}, {UI_MENU_OV_PER_FRAME, 888.8f},
                                     {UI_MENU_OV_FMT_PCT, 100}};
        ImGui::TableSetupColumn("##w0", ImGuiTableColumnFlags_WidthStretch);
        perf_column("##w1", c1, 3);
        perf_label(UI_MENU_OV_REPLAY, wait_c);
        perf_cell(UI_MENU_OV_FMT_MS, s.replay_ms);
        perf_label(UI_MENU_OV_GPU_TIME, wait_c);
        perf_cell(UI_MENU_OV_FMT_MS, s.gpu_ms);
        perf_label(UI_MENU_OV_PRESENT_TIME, wait_c);   // not in the row above: the present's own command buffer
        perf_cell(UI_MENU_OV_FMT_MS, s.present_ms);
        // the GPU row at the top counts every process; this is the game's own part, so work outside it shows apart
        if (s.gpu_game_pct >= 0) {
            perf_label(UI_MENU_OV_GPU_GAME, wait_c);
            perf_cell(UI_MENU_OV_FMT_PCT, s.gpu_game_pct);
        }
        perf_label(UI_MENU_OV_PIPE_WAIT, wait_c);
        perf_cell(UI_MENU_OV_FMT_MS, s.pipe_wait_ms);
        perf_label(UI_MENU_OV_SHADER_WAIT, wait_c);
        perf_cell(UI_MENU_OV_FMT_MS, s.xlate_ms);
        perf_label(UI_MENU_OV_TEX_UPLOADS, wait_c);
        perf_cell(UI_MENU_OV_PER_FRAME, s.tex_uploads);
        ImGui::EndTable();
    }
    perf_picture(t, pic_c);
    if (s.gpu_name[0]) ImGui::TextDisabled("%s", s.gpu_name);
    if (s.cpu_name[0]) ImGui::TextDisabled("%s", s.cpu_name);
    ImGui::End();
}

// The creator's hair pages hint (runtime/avatar_hair.c): the page button (the top face button: Triangle on a PlayStation pad, Space on the
// keyboard) turns the pages of the style grid. Drawn at the bottom left of the game picture while the Style grid is open, with the glyph the
// glyph system picks for the controller and style in use (a tile of the glyph art pack, or a plain round button for the native style).
extern "C" int avatar_hair_hint(int *page, int *male, int *source, int *family, int *symbol);
extern "C" int avatar_hair_hud(char *buf, int n);
extern "C" int avatar_hair_modify_ui(char lines[48][72], int *open, int *sel);
extern "C" const char *avatar_hair_shot_tag(void);
extern "C" int avatar_hair_listbar(float *pos, float *len);
extern "C" int avatar_hair_dialog(void);

// The glyph of a creator button symbol, size x size, centred on (cx, cy): the art tile of the glyph art pack fitted to a 64 x 64 cell and drawn as
// runs of rectangles (the overlay has no textures), or for the native style a round button with its label. The cells are kept, a panel draws several.
static const char *sym_label(int s) {
    switch (s) {
        case SYM_X: return "X";  case SYM_Y: return "Y";  case SYM_A: return "A";  case SYM_B: return "B";
        case SYM_L: return "L";  case SYM_R: return "R";  case SYM_ZL: return "ZL"; case SYM_ZR: return "ZR";
        case SYM_MNS: return "-"; case SYM_CRSUD: return "U/D"; case SYM_CRSLR: return "L/R";
        default: return "?";
    }
}

// A glyph tile fitted to a box up to three times as wide as it is high, cropped to its visible pixels. A tile that is much taller than wide (two keys one
// above the other, the keyboard's up and down) is cut in the middle and set side by side, left to right. Resampled to the size it is drawn at, in whole
// pixels, so every texel is drawn as one screen pixel at a whole position: fitting a fixed 64 x 64 cell and stretching its texels by a fraction left
// uneven edges and seams, which looked grainy.
struct GlyphTile { int a, n, w, h; uint32_t px[128 * 64]; };
static int16_t g_art[GLYPH_SYMBOLS];   // the glyph system's tiles for the controller and style in use, refreshed once per drawn frame
static bool g_kbd;                      // the keyboard style is the one in use
// Refreshes g_art for the panels at the bottom left. On the keyboard a button bound to two keys (fine and coarse: Z or the right mouse button, F or X) has no tile in
// the glyph system, which then falls back to the game's own round button; here the first of its keys that is not a mouse button stands for it.
static void glyph_refresh() {
    glyph_current_symbol_set(g_art);
    int source, family, layout, pref = glyph_style_setting();
    static InputMap map;
    input_glyph_snapshot(&source, &family, &layout, &map);
    g_kbd = pref == GLYPH_KEYBOARD || (pref == GLYPH_AUTO && source == GLYPH_SOURCE_KEYBOARD);
    if (!g_kbd) return;
    static const struct { int symbol, action; } two[] = {{SYM_ZL, B_ZL}, {SYM_ZR, B_ZR}};
    for (const auto &z : two) {
        if (g_art[z.symbol] >= 0) continue;
        uint8_t keys[INPUT_BINDING_MAX];
        int n = input_map_keys_of(&map, z.action, keys, sizeof keys);
        for (int i = 0; i < n; i++) {
            if (keys[i] == KEY_NONE || (keys[i] >= KEY_MOUSE_L && keys[i] <= KEY_MOUSE_X2)) continue;
            int a = glyph_art_find(GLYPH_KEYBOARD, keys[i]);
            if (a < 0) continue;
            g_art[z.symbol] = (int16_t)glyph_art_variant(a, glyph_appearance_setting());
            break;
        }
    }
}
static const GlyphTile *glyph_tile(int a, int N) {
    enum { SLOTS = 48 };
    static GlyphTile cache[SLOTS];
    static uint32_t tmp[128 * 128];
    static int used;
    for (int k = 0; k < used; k++) if (cache[k].a == a && cache[k].n == N) return &cache[k];
    if (used >= SLOTS) used = 0;
    auto bounds = [&](int cw, int ch, int b[4]) {
        b[0] = cw; b[1] = ch; b[2] = b[3] = -1;
        for (int y = 0; y < ch; y++) for (int x = 0; x < cw; x++)
            if (tmp[y * cw + x] >> 24) { b[0] = std::min(b[0], x); b[1] = std::min(b[1], y); b[2] = std::max(b[2], x); b[3] = std::max(b[3], y); }
    };
    int CW = std::min(128, N * 3), CH = N, b[4];
    if (!glyph_art_cell((unsigned)a, CW, CH, 0, 0, CW, CH, tmp)) return nullptr;
    bounds(CW, CH, b);
    if (b[2] < b[0]) return nullptr;
    int bw = b[2] - b[0] + 1, bh = b[3] - b[1] + 1;
    GlyphTile &g = cache[used];
    g.a = a; g.n = N;
    if (bh * 10 > bw * 17) {   // two keys stacked: refit twice as high, then the upper half goes left and the lower half right
        CW = N; CH = N * 2;
        if (!glyph_art_cell((unsigned)a, CW, CH, 0, 0, CW, CH, tmp)) return nullptr;
        bounds(CW, CH, b);
        if (b[2] < b[0]) return nullptr;
        bw = b[2] - b[0] + 1; bh = b[3] - b[1] + 1;
        int top = bh / 2, bot = bh - top, gap = std::max(2, N / 10);
        // each half cropped to its own visible rows, then the two centred on each other (the keys are not the same height)
        auto rows = [&](int y0, int n, int *first, int *last) {
            *first = n; *last = -1;
            for (int y = 0; y < n; y++) for (int x = 0; x < bw; x++) if (tmp[(y0 + y) * CW + b[0] + x] >> 24) { *first = std::min(*first, y); *last = std::max(*last, y); }
        };
        int t0, t1, u0, u1;
        rows(b[1], top, &t0, &t1);
        rows(b[1] + top, bot, &u0, &u1);
        if (t1 < t0 || u1 < u0) return nullptr;
        int th = t1 - t0 + 1, uh = u1 - u0 + 1;
        g.w = bw * 2 + gap; g.h = std::max(th, uh);
        if (g.w > 128 || g.h > 64) return nullptr;
        memset(g.px, 0, sizeof g.px);
        for (int y = 0; y < th; y++) for (int x = 0; x < bw; x++) g.px[((g.h - th) / 2 + y) * g.w + x] = tmp[(b[1] + t0 + y) * CW + b[0] + x];
        for (int y = 0; y < uh; y++) for (int x = 0; x < bw; x++) g.px[((g.h - uh) / 2 + y) * g.w + bw + gap + x] = tmp[(b[1] + top + u0 + y) * CW + b[0] + x];
    } else {
        g.w = bw; g.h = bh;
        if (g.w > 128 || g.h > 64) return nullptr;
        for (int y = 0; y < bh; y++) for (int x = 0; x < bw; x++) g.px[y * bw + x] = tmp[(b[1] + y) * CW + b[0] + x];
    }
    used++;
    return &g;
}

// The glyph of a creator button symbol with its left edge at x, centred on cy, size pixels high (a wide tile is wider): returns the width it takes (dl null:
// only measures). The art tile of the glyph art pack is drawn as runs of rectangles (the overlay has no textures); the native style gets a round button
// with its label.
static float glyph_at(ImDrawList *dl, int symbol, float x, float cy, float size) {
    if (g_kbd && (symbol == SYM_CRSUD || symbol == SYM_CRSLR)) {   // two single keys side by side (clearer than the pack's stacked tile)
        int s1 = symbol == SYM_CRSUD ? SYM_CRSU : SYM_CRSL, s2 = symbol == SYM_CRSUD ? SYM_CRSD : SYM_CRSR;
        if (g_art[s1] >= 0 && g_art[s2] >= 0) {
            float gap = std::max(2.f, size * 0.1f), w1 = glyph_at(dl, s1, x, cy, size);
            return w1 + gap + glyph_at(dl, s2, x + w1 + gap, cy, size);
        }
    }
    int a = symbol >= 0 && symbol < GLYPH_SYMBOLS ? g_art[symbol] : -1;
    int N = std::max(8, std::min((int)(size + 0.5f), 64));
    const GlyphTile *g = a >= 0 ? glyph_tile(a, N) : nullptr;
    if (g) {
        if (dl) {
            float x0 = std::floor(x + 0.5f), y0 = std::floor(cy - g->h / 2.f + 0.5f);
            for (int y = 0; y < g->h; y++)
                for (int px = 0; px < g->w;) {
                    uint32_t p = g->px[y * g->w + px];
                    int x1 = px + 1;
                    if (!(p >> 24)) { px = x1; continue; }
                    while (x1 < g->w && g->px[y * g->w + x1] == p) x1++;
                    dl->AddRectFilled(ImVec2(x0 + (float)px, y0 + (float)y), ImVec2(x0 + (float)x1, y0 + (float)(y + 1)), IM_COL32(p & 255, p >> 8 & 255, p >> 16 & 255, p >> 24));
                    px = x1;
                }
        }
        return (float)g->w;
    }
    if (dl) {
        float cx = x + size / 2, r = size * 0.5f - 0.5f;
        dl->AddCircleFilled(ImVec2(cx, cy), r, IM_COL32(245, 250, 252, 255), 28);
        dl->AddCircle(ImVec2(cx, cy), r, IM_COL32(110, 120, 130, 255), 28, std::max(1.f, r * 0.1f));
        const char *lab = sym_label(symbol);
        ImVec2 ls = ImGui::CalcTextSize(lab);
        float f = std::min(r * 1.2f / ls.y, r * 1.5f / ls.x);
        dl->AddText(nullptr, ImGui::GetFontSize() * f, ImVec2(cx - ls.x * f / 2, cy - ls.y * f / 2), IM_COL32(30, 42, 56, 255), lab);
    }
    return size;
}

static void hint_glyph(ImDrawList *dl, int symbol, float cx, float cy, float size) {   // centred on cx
    float w = glyph_at(nullptr, symbol, 0, cy, size);
    glyph_at(dl, symbol, cx - w / 2, cy, size);
}

static void hud_draw(const MenuTarget *t, float scale) {   // XCX_HAIR_HUD=1: what the hairstyle fit did to the hairstyle under the cursor
    char buf[320];
    if (!avatar_hair_hud(buf, sizeof buf)) return;
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    ImVec2 sz = ImGui::CalcTextSize(buf);
    float pad = 6 * scale;
    ImVec2 p(12 * scale, (float)t->height - 40 * scale - sz.y - pad * 2);
    dl->AddRectFilled(p, ImVec2(p.x + sz.x + pad * 2, p.y + sz.y + pad * 2), IM_COL32(0, 0, 0, 170), 4 * scale);
    dl->AddText(ImVec2(p.x + pad, p.y + pad), IM_COL32(255, 235, 120, 255), buf);
}

// The Modify window (runtime/avatar_hair.c): the creator's category list has a Modify row, and while the cursor is on it this window takes the place of the
// grid the other rows show there. It lists every part of the hair with its values, the selected row marked, and the buttons that work in it as the glyphs
// of the controller in use. A line starting with '#' names a part; a line starting with '@' is a line of buttons: "sym[+sym]=label;..." (symbols as
// numbers); a value starting with '@' is one button.
// dl null: only measures (the returned x is where the line ends). colw > 0: the entries start at fixed distances of colw from x (columns), and *over is raised to the
// largest share of its column an entry takes (above 1: it does not fit).
static float glyph_line(ImDrawList *dl, const char *spec, float x, float y, float rh, float f, float gap, float colw = 0.f, float *over = nullptr) {
    float g = rh * 0.92f, gp = std::max(2.f, g * 0.1f), x0 = x;
    float k = f / ImGui::GetFontSize();
    int col = 0;
    for (const char *p = spec; *p; col++) {
        if (colw > 0.f) x = x0 + colw * (float)col;
        float xs = x;
        int a = atoi(p), b = -1;
        while (*p && *p != '+' && *p != '=' && *p != ';') p++;
        if (*p == '+') { b = atoi(p + 1); while (*p && *p != '=' && *p != ';') p++; }
        char label[24] = {0};
        if (*p == '=') { p++; int n = 0; while (*p && *p != ';' && n < 23) label[n++] = *p++; }
        if (*p == ';') p++;
        x += glyph_at(dl, a, x, y + rh / 2, g) + gp;
        if (b >= 0) x += glyph_at(dl, b, x, y + rh / 2, g) + gp;
        if (dl) dl->AddText(nullptr, f, ImVec2(x + 3, y + (rh - f) / 2), IM_COL32(190, 230, 242, 255), label);
        x += 3 + ImGui::CalcTextSize(label).x * k;
        if (colw > 0.f && over) *over = std::max(*over, (x - xs) / colw);
        x += gap;
    }
    return x;
}

static bool modify_draw(const MenuTarget *t) {
    char lines[48][72];
    int open = 0, sel = -1;
    int n = avatar_hair_modify_ui(lines, &open, &sel);
    if (n <= 0) return false;
    // the creator's picture fills the window: the grid of the other rows is at 0.339 to 0.5215 of its width and 0.204 to 0.844 of its height
    float W = (float)t->width, H = (float)t->height;
    float fs = std::min(H / 1080.f, W / 1920.f) * 22.f, rh = fs * 1.3f, pad = fs * 0.5f, hb_pad = pad;
    float x0 = W * 0.339f, w = W * 0.1825f, y0 = H * 0.2044f, h = H * 0.64f;
    int nb = 0;
    int body[48];
    for (int i = 0; i < n; i++) if (lines[i][0] != '@') body[nb++] = i;   // the lines of buttons are drawn by legend_draw, outside the window
    int fit = std::max(1, (int)((h - pad * 2 - hb_pad) / rh));
    static int first;
    int selb = -1;
    for (int j = 0; j < nb; j++) if (body[j] == sel) selb = j;
    if (selb >= 0) {   // keep the selected row in view (and the part's name above the first row)
        if (selb < first) first = selb;
        if (selb >= first + fit) first = selb - fit + 1;
        if (selb > 0 && lines[body[selb - 1]][0] == '#' && first > selb - 1) first = selb - 1;
    } else if (!open) first = 0;
    first = std::max(0, std::min(first, std::max(0, nb - fit)));
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    float hb = std::max(3.f, h * 0.0095f), x1 = x0 + w, y1 = y0 + h;
    // The game's "Discard current edits" question is a panel of the game's own picture: it covers part of this window, as it covers the grid of the other rows. The window is
    // painted in the three pieces that stay clear of its rectangle (measured on the grid's rectangle: it starts 0.197 of the grid's width in and 0.262 of its height down,
    // ends 1.56 widths in and 0.661 heights down).
    ImVec2 dlg0(x0 + w * 0.197f - 2.f, y0 + h * 0.262f - 2.f), dlg1(x0 + w * 1.56f + 2.f, y0 + h * 0.661f + 2.f);
    int pieces = avatar_hair_dialog() ? 3 : 1;
    for (int piece = 0; piece < pieces; piece++) {
        if (pieces == 3) {
            if (piece == 0) dl->PushClipRect(ImVec2(0, 0), ImVec2(dlg0.x, H), true);
            else if (piece == 1) dl->PushClipRect(ImVec2(dlg0.x, 0), ImVec2(W, dlg0.y), true);
            else dl->PushClipRect(ImVec2(dlg0.x, dlg1.y), ImVec2(W, H), true);
        }
        // the look of the game's own panels (colours read from its grid panel): an even dark body, a slate header band with a raised notch at the left and two small squares
        // at the right, a thin band at the bottom
        const ImU32 body_c = IM_COL32(16, 20, 30, 252);
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), body_c);
        dl->AddRectFilledMultiColor(ImVec2(x0, y0 + hb), ImVec2(x1, y0 + hb * 4.f), IM_COL32(34, 46, 58, 252), IM_COL32(34, 46, 58, 252), body_c, body_c);
        dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(32, 42, 54, 255), 0.f, 0, 1.5f);
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y0 + hb), IM_COL32(78, 96, 117, 255));
        dl->AddRectFilled(ImVec2(x0, y0 - hb * 0.7f), ImVec2(x0 + w * 0.34f, y0 + hb), IM_COL32(72, 87, 107, 255));
        dl->AddRectFilled(ImVec2(x1 - w * 0.12f, y0 + hb * 0.22f), ImVec2(x1 - w * 0.12f + hb * 0.55f, y0 + hb * 0.77f), IM_COL32(108, 137, 156, 255));
        dl->AddRectFilled(ImVec2(x1 - w * 0.07f, y0 + hb * 0.22f), ImVec2(x1 - w * 0.07f + hb * 0.55f, y0 + hb * 0.77f), IM_COL32(108, 137, 156, 255));
        dl->AddRectFilled(ImVec2(x0, y1 - hb * 0.5f), ImVec2(x1, y1), IM_COL32(79, 94, 114, 255));
        float y = y0 + hb + pad * 0.6f;
        for (int j = first; j < nb && j < first + fit; j++) {
            int i = body[j];
            if (lines[i][0] == '#') {
                dl->AddText(nullptr, fs, ImVec2(x0 + pad, y + (rh - fs) / 2), IM_COL32(150, 215, 232, 255), lines[i] + 1);
                dl->AddLine(ImVec2(x0 + pad, y + rh - 1), ImVec2(x0 + w - pad, y + rh - 1), IM_COL32(120, 215, 235, 110), 1.f);
                y += rh;
                continue;
            }
            if (i == sel) {
                dl->AddRectFilled(ImVec2(x0 + pad * 0.4f, y), ImVec2(x0 + w - pad * 0.4f, y + rh), IM_COL32(25, 70, 78, 255));
                dl->AddRect(ImVec2(x0 + pad * 0.4f, y), ImVec2(x0 + w - pad * 0.4f, y + rh), IM_COL32(44, 246, 246, 255), 0.f, 0, std::max(1.5f, fs * 0.09f));
            }
            // "label\tvalue\tposition": the name, the value and where it lies on its slider (0 to 1)
            char buf[72];
            snprintf(buf, sizeof buf, "%s", lines[i]);
            char *tab = strchr(buf, '\t');
            const char *val = "";
            float frac = -1.f;
            if (tab) {
                *tab = 0; val = tab + 1;
                char *t2 = strchr(tab + 1, '\t');
                if (t2) { *t2 = 0; frac = (float)atof(t2 + 1); }
            }
            bool on = i == sel;
            dl->AddText(nullptr, fs, ImVec2(x0 + pad * 1.4f, y + (rh - fs) / 2), on ? IM_COL32(245, 250, 252, 255) : IM_COL32(205, 215, 222, 255), buf);
            float k = fs / ImGui::GetFontSize();
            float vw = *val ? ImGui::CalcTextSize(val).x * k : 0.f;
            if (*val) dl->AddText(nullptr, fs, ImVec2(x0 + w - pad * 1.6f - vw, y + (rh - fs) / 2), on ? IM_COL32(255, 255, 255, 255) : IM_COL32(215, 222, 228, 255), val);
            if (frac >= 0.f) {   // the slider: a thin track, the part up to the thumb lit, a thumb that glows while the row is selected (like the creator's own sliders)
                float sx0 = x0 + w * 0.40f, sx1 = x0 + w - pad * 1.6f - std::max(vw, w * 0.17f) - pad * 0.8f, cy = y + rh / 2;
                float th = std::max(1.5f, rh * 0.07f), tx = sx0 + (sx1 - sx0) * std::max(0.f, std::min(1.f, frac));
                dl->AddRectFilled(ImVec2(sx0, cy - th / 2), ImVec2(sx1, cy + th / 2), IM_COL32(58, 64, 72, 255));
                dl->AddRectFilled(ImVec2(sx0, cy - th / 2), ImVec2(tx, cy + th / 2), on ? IM_COL32(32, 205, 205, 255) : IM_COL32(26, 120, 124, 255));
                float cx = sx0 + (sx1 - sx0) * 0.5f;
                dl->AddRectFilled(ImVec2(cx - 0.5f, cy - rh * 0.14f), ImVec2(cx + 0.5f, cy + rh * 0.14f), IM_COL32(95, 105, 115, 255));
                float tw = std::max(3.f, rh * 0.13f), tt = rh * 0.30f;
                if (on) {
                    dl->AddRectFilled(ImVec2(tx - tw * 2.2f, cy - tt * 1.15f), ImVec2(tx + tw * 2.2f, cy + tt * 1.15f), IM_COL32(44, 246, 246, 60), tw);
                    dl->AddRectFilled(ImVec2(tx - tw * 1.4f, cy - tt * 1.05f), ImVec2(tx + tw * 1.4f, cy + tt * 1.05f), IM_COL32(44, 246, 246, 90), tw * 0.7f);
                }
                dl->AddRectFilled(ImVec2(tx - tw / 2, cy - tt), ImVec2(tx + tw / 2, cy + tt), on ? IM_COL32(255, 255, 255, 255) : IM_COL32(190, 200, 208, 255));
            }
            y += rh;
        }
        if (nb > fit) {   // a scroll bar, like the grid's
            float bx = x0 + w - pad * 0.45f, top = y0 + pad, hh = fit * rh;
            dl->AddRectFilled(ImVec2(bx, top), ImVec2(bx + 3, top + hh), IM_COL32(28, 37, 44, 255));
            dl->AddRectFilled(ImVec2(bx, top + hh * (float)first / (float)nb), ImVec2(bx + 3, top + hh * (float)(first + fit) / (float)nb), IM_COL32(79, 94, 114, 255));
        }
        if (pieces == 3) dl->PopClipRect();
    }
    return open != 0;
}

// The category list's scroll bar, placed against the grid's rectangle like the window (the list panel ends just left of it): the track and thumb colours of the grid's own bar.
static void listbar_draw(const MenuTarget *t) {
    float pos, len;
    if (!avatar_hair_listbar(&pos, &len)) return;
    float W = (float)t->width, H = (float)t->height;
    float gx = W * 0.339f, gw = W * 0.1825f, gy = H * 0.2044f, gh = H * 0.64f;
    float bw = std::max(3.f, gw * 0.013f), bx = gx - gw * 0.058f - bw, top = gy + gh * 0.03f, hh = gh * 0.79f;   // thin, and wholly inside the list panel (its right edge is at gx - 0.040 gw)
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(ImVec2(bx, top), ImVec2(bx + bw, top + hh), IM_COL32(40, 52, 62, 255), bw * 0.4f);
    dl->AddRectFilled(ImVec2(bx, top + hh * pos), ImVec2(bx + bw, top + hh * (pos + len)), IM_COL32(120, 146, 166, 255), bw * 0.4f);
}

// The buttons that work in the Modify window, as lines of glyphs and labels in a panel at the bottom left of the picture (where the page hint sits while the
// Style grid is open): the window itself is too narrow for glyphs of a readable size.
static void legend_draw(const MenuTarget *t, bool f1) {
    char lines[48][72];
    int open = 0, sel = -1;
    int n = avatar_hair_modify_ui(lines, &open, &sel);
    if (f1 && !open) return;   // with the F1 menu open the buttons stay only while the editing window is open
    int idx[8], nf = 0;
    for (int i = 0; i < n && nf < 8; i++) if (lines[i][0] == '@') idx[nf++] = i;
    if (!nf) return;
    float gh = t->image_h > 0 ? (float)t->image_h : 9.f, gw = t->image_w > 0 ? (float)t->image_w : 16.f;
    float ph = gh * std::min((float)t->width / gw, (float)t->height / gh);
    // The same size for every controller and style: three fixed columns, the panel no wider than the category list's panel above it (its right edge is at 0.327 of
    // the picture's width). Only an entry that does not fit its column makes everything smaller.
    float f = ImGui::GetFontSize() * ph / 1080.f * 1.05f, lr, pad, gap, w, h, colw;
    const float pw = (float)t->width * 0.275f;
    for (int pass = 0; pass < 3; pass++) {
        lr = f * 1.9f; pad = f * 0.5f; gap = f * 0.9f;
        colw = (pw - pad * 2) / 3.f;
        float over = 0.f;
        for (int k = 0; k < nf; k++) glyph_line(nullptr, lines[idx[k]] + 1, 0, 0, lr, f, 0.f, colw, &over);
        if (over <= 1.f) break;
        f /= over * 1.02f;
    }
    w = pw; h = lr * (float)nf + pad * 2;
    ImVec2 p0((float)t->width * 0.012f, (float)t->height * 0.985f - h), p1(p0.x + w, p0.y + h);
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(p0, p1, IM_COL32(6, 14, 22, 215), pad * 0.7f);
    dl->AddRect(p0, p1, IM_COL32(120, 215, 235, 220), pad * 0.7f, 0, std::max(1.f, pad * 0.12f));
    for (int k = 0; k < nf; k++) glyph_line(dl, lines[idx[k]] + 1, p0.x + pad, p0.y + pad + lr * (float)k, lr, f, 0.f, colw, nullptr);
}

static void corner_draw(const MenuTarget *t, bool f1);
static void hint_draw(const MenuTarget *t, float scale) {
    hud_draw(t, scale);
    modify_draw(t);
    listbar_draw(t);
    corner_draw(t, false);
}

// The panels at the bottom left of the picture: the Modify window's buttons (legend), or the page hint of the Style grid. The page hint stays while the F1 menu is open (the Modify window does not, so neither does its legend).
static void corner_draw(const MenuTarget *t, bool f1) {
    glyph_refresh();
    legend_draw(t, f1);
    int page, male, source, family, symbol;
    if (!avatar_hair_hint(&page, &male, &source, &family, &symbol)) return;
    static int logged_page = -1, logged_source = -1, logged_symbol = -1;
    if (logged_page != page || logged_source != source || logged_symbol != symbol) {
        logged_page = page; logged_source = source; logged_symbol = symbol;
        int16_t art[GLYPH_SYMBOLS];
        glyph_current_symbol_set(art);
        rt_log("menu: hair page hint drawn: page %d, %s, family %d, button %s, glyph %s", page, source == GLYPH_SOURCE_KEYBOARD ? "keyboard" : "pad", family,
               symbol == SYM_Y ? "Y" : "X", art[symbol] >= 0 ? glyph_art_name((unsigned)art[symbol]) : "native");
    }
    float gw = t->image_w > 0 ? (float)t->image_w : 16.f, gh = t->image_h > 0 ? (float)t->image_h : 9.f;
    float s = std::min((float)t->width / gw, (float)t->height / gh), pw = gw * s, ph = gh * s;
    float ox = ((float)t->width - pw) / 2, oy = ((float)t->height - ph) / 2;
    const char *name = page == 2 ? "Story characters" : (page == 0) == (male != 0) ? "Male hairstyles" : "Female hairstyles";
    char line[96];
    snprintf(line, sizeof line, "%s  %d / 3", name, page + 1);
    const char *head = "Change page";
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    float u = ph / 1080.f * 1.35f;   // sized with the game picture
    float u1 = u * 0.78f;
    ImVec2 ts = ImGui::CalcTextSize(line), hs = ImGui::CalcTextSize(head);
    float tx = std::max(ts.x * u, hs.x * u1), th = ts.y * u, th1 = hs.y * u1, ic = (th + th1) * 1.15f, pad = th * 0.45f;
    float gw2 = ic * 0.9f;
    float w = pad + gw2 + pad + tx + pad, h = ic + pad;
    ImVec2 p0((float)t->width * 0.012f, (float)t->height * 0.985f - h), p1(p0.x + w, p0.y + h);   // the bottom left corner, under the Modify row
    dl->AddRectFilled(p0, p1, IM_COL32(6, 14, 22, 205), h * 0.25f);
    dl->AddRect(p0, p1, IM_COL32(120, 215, 235, 220), h * 0.25f, 0, std::max(1.f, h * 0.04f));
    hint_glyph(dl, symbol, p0.x + pad + gw2 / 2, p0.y + h / 2, ic * 0.95f);
    float x = p0.x + pad + gw2 + pad, y0 = p0.y + (h - th - th1) / 2;
    dl->AddText(nullptr, ImGui::GetFontSize() * u1, ImVec2(x, y0), IM_COL32(150, 215, 232, 255), head);
    dl->AddText(nullptr, ImGui::GetFontSize() * u, ImVec2(x, y0 + th1), IM_COL32(245, 250, 252, 255), line);
}

static void perf_draw(const MenuTarget *t, float scale) {
    if (g_perf_mode == 2) return perf_full_draw(t, scale);
    float w = (float)t->width;
    if (!g_perf_mode || !g_perf_text[0]) return;
    // with the menu open: under its window, over the dimming layout_menu drew before
    ImDrawList *dl = g_open.load(std::memory_order_relaxed) ? ImGui::GetBackgroundDrawList() : ImGui::GetForegroundDrawList();
    ImVec2 sz = ImGui::CalcTextSize(g_perf_text);
    float pad = 6 * scale;
    ImVec2 p(w - 12 * scale - sz.x - pad * 2, 12 * scale);   // the top right corner: the game's HUD leaves it empty
    dl->AddRectFilled(p, ImVec2(p.x + sz.x + pad * 2, p.y + sz.y + pad * 2), IM_COL32(0, 0, 0, 150), 4 * scale);
    dl->AddText(ImVec2(p.x + pad, p.y + pad), IM_COL32(255, 255, 255, 235), g_perf_text);
}

static bool hint_wanted() {
    int page, male, source, family, symbol;
    if (avatar_hair_hint(&page, &male, &source, &family, &symbol)) return true;
    char lines[48][72];
    int open = 0, sel = -1;
    if (avatar_hair_modify_ui(lines, &open, &sel) > 0) return true;   // the Modify window is shown too
    float pos, len;
    return avatar_hair_listbar(&pos, &len) != 0;   // and the list's scroll bar
}

extern "C" int menu_overlay_frame(uint64_t frame, const MenuTarget *t, const char **shot) {
    *shot = NULL;
    if (!g_test.parsed) test_parse();
    if (g_test.next_set < g_test.n_sets && frame >= g_test.sets[g_test.next_set].frame) test_apply_sets(frame);
    if (g_test.report_at && frame >= g_test.report_at) {   // XCX_BUG_REPORT_TEST: the button's action, no folder opened
        rt_log("menu: test hook: frame %llu: bug report%s", (unsigned long long)frame,
               g_test.report_save ? " with the save" : "");
        g_test.report_at = 0;
        g_report_focus = 5;
        report_start(g_test.report_save, false);
    }
    if (g_perf_save.exchange(0)) cfg_save();
    perf_tick(plat_now_ns(), t);
    bool open = g_open.load(std::memory_order_relaxed);
    bool text = g_text.load(std::memory_order_acquire);
    registered_route(open,text);

    if(!open || text || g_failed_flag())mod_ui_interaction(0);
    if (!open && !text) {
        if (g_save_pending) g_save_pending = opt_save_tick(1);   // closed: the last change goes to xcx.ini now
        if (g_test.at && !g_test.opened && frame >= g_test.at) {
            g_test.opened = frame;
            g_test.page = 0;
            g_tab_request = 0;
            set_open(1);
            open = true;
        } else if (!g_perf_mode && !hint_wanted()) {
            return 0;
        }
    }
    if (g_failed_flag()) {
        set_open(0);
        return 0;
    }
    uint64_t t0 = plat_now_ns();
    if (!g_ctx_ok) make_context();
    float scale = menu_scale(t->height);
    if (!vk_up(t) || (g_font_scale != scale && !fonts_up(scale))) {
        rt_log("menu: Dear ImGui Vulkan back end set-up failed; the menu stays closed");
        g_failed.store(1);
        mod_menu_support(-1);mod_menu_invalidate();
        mod_ui_interaction(0);
        set_open(0);
        return 0;
    }
    if (g_style_scale != scale) {
        ImGui::GetStyle() = g_base_style;
        ImGui::GetStyle().ScaleAllSizes(scale);
        g_style_scale = scale;
    }
    list_gpus(t->instance);
    ImGuiIO &io = ImGui::GetIO();
    int reset = g_reset.exchange(0);
    if (reset) {
        io.ClearInputKeys();
        io.ClearInputMouse();
        if (text) ImGui::ClearActiveID();   // a new request must load its own initial text
        g_last_ns = 0;
        g_focus_page = g_tab_request >= 0 ? g_tab_request : g_tab;   // opening: the first row has the cursor
        if (GImGui->OpenPopupStack.Size) ImGui::ClosePopupToLevel(0, false);   // a confirmation left open on closing
        g_modal = false;
    }
    io.DisplaySize = ImVec2((float)t->width, (float)t->height);
    double dt = g_last_ns ? (t0 - g_last_ns) / 1e9 : 1 / 60.0;
    io.DeltaTime = (float)(dt < 1e-4 ? 1e-4 : dt > 0.1 ? 0.1 : dt);
    g_last_ns = t0;
    int pad = 0;
    if (!open && !text) {   // the overlay alone: no input reaches ImGui
    } else if (text) {
        drain_events(io);
        pad = text_pad(reset);
        if (int a = text_test(io)) pad = a;
    } else if (g_test.page < 0) {
        drain_events(io);
        pad_events(io, reset);
    } else {   // the test hook's pages: the user's input in a background test window must not change settings
        plat_mutex_lock(&ev_lock);
        ev_n = 0;
        plat_mutex_unlock(&ev_lock);
    }
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
    if (text) {
        plat_mutex_lock(&text_lock);
        int answer = layout_text(t->width, t->height, scale, pad);
        if (answer && !g_text_answer) {
            g_text_answer = answer;
            if (cfg_get("XCX_SWKBD_TEST")) {   // the test hook keeps the frame that answered
                static char path[512];
                const char *dir = cfg_get("XCX_MENU_TEST_DIR");
                snprintf(path, sizeof path, "%s/swkbd-test-%llu.png", dir ? dir : ".", (unsigned long long)frame);
                *shot = path;
            }
        }
        plat_mutex_unlock(&text_lock);
    } else if (open && g_quit.load()) {
        layout_quit(t->width, t->height);
    } else if (open) {
        layout_menu(t->width, t->height, scale);
    }
    perf_draw(t, scale);
    if (open && !text) corner_draw(t, true);   // the F1 menu does not hide the bottom left panels
    if (!open && !text) {
        hint_draw(t, scale);
        if (const char *tag = avatar_hair_shot_tag()) {   // test hook (XCX_HAIR_MODNAV): this frame, with the overlay on it, under the step's name
            static char tpath[512];
            const char *dir = cfg_get("XCX_MENU_TEST_DIR");
            snprintf(tpath, sizeof tpath, "%s/%s.png", dir ? dir : ".", tag);
            *shot = tpath;
        } else if (cfg_get("XCX_HAIR_HINT_SHOT") && hint_wanted()) {   // test hook: a few frames with the page hint on them go to XCX_MENU_TEST_DIR
            static int n;
            static uint64_t last;
            static char hpath[512];
            if (n < std::max(3, atoi(cfg_get("XCX_HAIR_HINT_SHOT"))) && frame > last + 240) {
                const char *dir = cfg_get("XCX_MENU_TEST_DIR");
                snprintf(hpath, sizeof hpath, "%s/hint-shot-%llu.png", dir ? dir : ".", (unsigned long long)frame);
                *shot = hpath;
                last = frame;
                n++;
            }
        }
    }
    ImGui::Render();
    if (open) g_save_pending = opt_save_tick(0);
    g_cost_ns += plat_now_ns() - t0;
    if (g_test.page >= 0 && frame >= g_test.opened + g_test.per_page * (uint64_t)(g_test.page + 1)) {
        static char path[512];   // this frame's image goes to a file; then the next tab, or the menu closes
        const char *dir = cfg_get("XCX_MENU_TEST_DIR");
        snprintf(path, sizeof path, "%s/menu-test-%llu-%d.png", dir ? dir : ".", (unsigned long long)frame, g_test.page);
        *shot = path;
        if (++g_test.page == TAB_COUNT) {
            g_test.page = -1;
            set_open(0);   // the next frame lays out nothing
        } else {
            g_tab_request = g_test.page;
        }
    }
    return ImGui::GetDrawData()->TotalVtxCount > 0;
}

extern "C" void menu_overlay_draw(VkCommandBuffer cb) {
    uint64_t t0 = plat_now_ns();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cb);
    g_cost_ns += plat_now_ns() - t0;
    if (++g_cost_frames == 600) {
        rt_log("menu: %.3f ms CPU per open frame (layout and recording, mean of 600)", g_cost_ns / 600 / 1e6);
        g_cost_ns = g_cost_frames = 0;
    }
}
