/*
 * hwgl - an opengl32.dll shim that lets OpenGL 3.x Windows games run on macOS under Wine/CrossOver.
 *
 * macOS only offers two kinds of OpenGL context: legacy 2.1, or forward-compatible core 3.2-4.1.
 * Many Windows games assume what Windows drivers give them, and break in two ways:
 *
 *  1. They request a 3.x *compatibility* profile (or no forward-compatible flag). Wine's Mac
 *     driver rejects that. We rewrite any 3.x request into forward-compatible core 3.2+.
 *
 *  2. They load function pointers and probe capabilities (often through GLEW) while their
 *     bootstrap legacy context is current. On macOS that context is GL 2.1, so Wine returns NULL
 *     for every 3.x+ function and the game picks the wrong code paths. We hand out a
 *     forward-compatible core context from wglCreateContext instead, so everything the game
 *     learns during bootstrap matches the real context it creates afterwards.
 *
 * Additionally, glGetString(GL_EXTENSIONS) returns NULL in core contexts; we synthesise the
 * string from glGetStringi so older extension loaders keep working.
 *
 * Every other export jumps straight to the system opengl32.dll (see gen_stubs.py).
 *
 * Built freestanding: no Windows SDK, no C runtime.
 */

#define DLL_PROCESS_ATTACH 1

#define GL_EXTENSIONS 0x1F03
#define GL_NUM_EXTENSIONS 0x821D

#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_FLAGS_ARB 0x2094
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB 0x0002
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x0001

#define MAX_ATTRIB_PAIRS 32
#define EXT_BUF_SIZE 32768

typedef void *HANDLE;
typedef void *PROC;

__declspec(dllimport) HANDLE __stdcall LoadLibraryA(const char *name);
__declspec(dllimport) PROC __stdcall GetProcAddress(HANDLE module, const char *name);
__declspec(dllimport) unsigned __stdcall GetSystemDirectoryA(char *buf, unsigned size);
__declspec(dllimport) void __stdcall OutputDebugStringA(const char *msg);

/* Filled by the generated stubs.S. */
extern PROC real_fns[];
extern const char *const export_names[];
extern const unsigned export_count;

static PROC (__stdcall *real_wglGetProcAddress)(const char *);
static HANDLE (__stdcall *real_wglGetCurrentContext)(void);
static HANDLE (__stdcall *real_wglGetCurrentDC)(void);
static int (__stdcall *real_wglMakeCurrent)(HANDLE, HANDLE);
static int (__stdcall *real_wglDeleteContext)(HANDLE);
static const unsigned char *(__stdcall *real_glGetString)(unsigned);
static unsigned (__stdcall *real_glGetError)(void);
static void (__stdcall *real_glGetIntegerv)(unsigned, int *);

static HANDLE (__stdcall *real_wglCreateContext)(HANDLE);
static HANDLE (__stdcall *real_wglCreateContextAttribsARB)(HANDLE, HANDLE, const int *);

static char ext_buf[EXT_BUF_SIZE];
static int ext_built;

int _fltused;

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

static unsigned str_len(const char *s)
{
    unsigned n = 0;
    while (s[n]) n++;
    return n;
}

static void log_msg(const char *a, const char *b)
{
    char buf[256];
    unsigned n = 0;
    const char *parts[3] = { "hwgl: ", a, b };
    for (int p = 0; p < 3; p++)
        for (const char *s = parts[p]; s && *s && n < sizeof(buf) - 2; s++) buf[n++] = *s;
    buf[n++] = '\n';
    buf[n] = 0;
    OutputDebugStringA(buf);
}

static PROC real_fn(const char *name)
{
    for (unsigned i = 0; i < export_count; i++)
        if (str_eq(export_names[i], name)) return real_fns[i];
    return 0;
}

static int load_real_opengl32(HANDLE self)
{
    static const char tail[] = "\\opengl32.dll";
    char path[1024];
    unsigned room = sizeof(path) - sizeof(tail);
    /* Returns the length on success, or the required size if the buffer is too small. */
    unsigned n = GetSystemDirectoryA(path, room);
    if (!n || n >= room) {
        log_msg("system directory path is unavailable or too long", 0);
        return 0;
    }
    for (unsigned i = 0; i <= str_len(tail); i++) path[n + i] = tail[i];

    HANDLE real = LoadLibraryA(path);
    if (!real || real == self) {
        log_msg("could not load the system opengl32.dll from ", path);
        return 0;
    }
    for (unsigned i = 0; i < export_count; i++)
        real_fns[i] = GetProcAddress(real, export_names[i]);

    real_wglGetProcAddress = real_fn("wglGetProcAddress");
    real_wglGetCurrentContext = real_fn("wglGetCurrentContext");
    real_wglGetCurrentDC = real_fn("wglGetCurrentDC");
    real_wglCreateContext = real_fn("wglCreateContext");
    real_wglMakeCurrent = real_fn("wglMakeCurrent");
    real_wglDeleteContext = real_fn("wglDeleteContext");
    real_glGetString = real_fn("glGetString");
    real_glGetError = real_fn("glGetError");
    real_glGetIntegerv = real_fn("glGetIntegerv");
    log_msg("loaded, forwarding to ", path);
    return 1;
}

int __stdcall DllMain(HANDLE self, unsigned reason, void *reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) return load_real_opengl32(self);
    return 1;
}

typedef HANDLE (__stdcall *create_attribs_fn)(HANDLE, HANDLE, const int *);

/* Fix 1: turn any 3.x request into something macOS accepts. */
static HANDLE __stdcall hw_wglCreateContextAttribsARB(HANDLE dc, HANDLE share, const int *attribs)
{
    int major = 1, minor = 0, flags = 0, profile = 0;
    int other[MAX_ATTRIB_PAIRS * 2], n_other = 0;
    int out[MAX_ATTRIB_PAIRS * 2 + 1], n = 0;

    for (const int *a = attribs; a && a[0] && n_other < MAX_ATTRIB_PAIRS * 2 - 2; a += 2) {
        switch (a[0]) {
        case WGL_CONTEXT_MAJOR_VERSION_ARB: major = a[1]; break;
        case WGL_CONTEXT_MINOR_VERSION_ARB: minor = a[1]; break;
        case WGL_CONTEXT_FLAGS_ARB: flags = a[1]; break;
        case WGL_CONTEXT_PROFILE_MASK_ARB: profile = a[1]; break;
        default: other[n_other++] = a[0]; other[n_other++] = a[1]; break;
        }
    }

    if (major >= 3) {
        if (major == 3 && minor < 2) minor = 2;
        if (profile != WGL_CONTEXT_CORE_PROFILE_BIT_ARB || !(flags & WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB))
            log_msg("rewriting context request to forward-compatible core profile", 0);
        flags |= WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB;
        profile = WGL_CONTEXT_CORE_PROFILE_BIT_ARB;
    }

    out[n++] = WGL_CONTEXT_MAJOR_VERSION_ARB; out[n++] = major;
    out[n++] = WGL_CONTEXT_MINOR_VERSION_ARB; out[n++] = minor;
    if (flags) { out[n++] = WGL_CONTEXT_FLAGS_ARB; out[n++] = flags; }
    if (profile) { out[n++] = WGL_CONTEXT_PROFILE_MASK_ARB; out[n++] = profile; }
    for (int i = 0; i < n_other; i++) out[n++] = other[i];
    out[n] = 0;

    HANDLE ctx = real_wglCreateContextAttribsARB(dc, share, out);
    if (!ctx) log_msg("wglCreateContextAttribsARB failed even after rewriting", 0);
    return ctx;
}

/* Fix 2: hand out a core context where the game asks for a legacy (bootstrap) one, so function
 * pointers and capability checks made under it reflect what the real 3.x context will support. */
HANDLE __stdcall hw_wglCreateContext(HANDLE dc)
{
    static const int core_attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 3, WGL_CONTEXT_MINOR_VERSION_ARB, 2,
        WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB, 0
    };
    HANDLE legacy = real_wglCreateContext(dc);
    if (!legacy) return 0;

    /* wglCreateContextAttribsARB can only be looked up with a context current. */
    if (!real_wglCreateContextAttribsARB) {
        HANDLE prev_ctx = real_wglGetCurrentContext(), prev_dc = real_wglGetCurrentDC();
        if (real_wglMakeCurrent(dc, legacy)) {
            real_wglCreateContextAttribsARB = (create_attribs_fn)real_wglGetProcAddress("wglCreateContextAttribsARB");
            real_wglMakeCurrent(prev_dc, prev_ctx);
        }
    }
    if (!real_wglCreateContextAttribsARB) return legacy;

    HANDLE core = real_wglCreateContextAttribsARB(dc, 0, core_attribs);
    if (!core) {
        log_msg("could not create core context, keeping legacy context", 0);
        return legacy;
    }
    real_wglDeleteContext(legacy);
    log_msg("upgraded legacy context to forward-compatible core", 0);
    return core;
}

PROC __stdcall hw_wglGetProcAddress(const char *name)
{
    if (!name) return 0;
    if (str_eq(name, "wglCreateContextAttribsARB")) {
        PROC p = real_wglGetProcAddress(name);
        if (!p) return 0;
        real_wglCreateContextAttribsARB = (create_attribs_fn)p;
        return (PROC)hw_wglCreateContextAttribsARB;
    }
    return real_wglGetProcAddress(name);
}

/* Extra: core contexts have no GL_EXTENSIONS string; build one from glGetStringi. */
const unsigned char *__stdcall hw_glGetString(unsigned name)
{
    const unsigned char *s = real_glGetString(name);
    if (s || name != GL_EXTENSIONS) return s;

    if (!ext_built) {
        const unsigned char *(__stdcall *getstringi)(unsigned, unsigned) = real_wglGetProcAddress("glGetStringi");
        int count = 0;
        unsigned n = 0;
        if (!getstringi) return 0;
        real_glGetError(); /* clear the GL_INVALID_ENUM the failed query raised */
        real_glGetIntegerv(GL_NUM_EXTENSIONS, &count);
        for (int i = 0; i < count; i++) {
            const unsigned char *ext = getstringi(GL_EXTENSIONS, i);
            if (!ext) continue;
            unsigned len = str_len((const char *)ext);
            if (n + len + 2 >= EXT_BUF_SIZE) break;
            for (unsigned j = 0; j < len; j++) ext_buf[n++] = ext[j];
            ext_buf[n++] = ' ';
        }
        ext_buf[n] = 0;
        ext_built = 1;
    } else {
        real_glGetError();
    }
    return (const unsigned char *)ext_buf;
}
