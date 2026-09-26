// Stand-ins for the vendor application's exports, built as one shared library per variant. They
// differ only in how onEasyUIDeinit appears in the dynamic symbol table.
namespace base {
bool wifiOnAndWait(int seconds) { return seconds > 0; }
}

extern "C" {
void onEasyUIInit(void*) {}
const char* onStartupApp(void*) { return "mainActivity"; }
#if defined(TC002_FIXTURE_FULL)
void onEasyUIDeinit(void*) {}
#elif defined(TC002_FIXTURE_WEAK)
__attribute__((weak)) void onEasyUIDeinit(void*) {}
#elif defined(TC002_FIXTURE_UNDEFINED)
// Only referenced: the name is in .dynsym, but as an undefined import.
void onEasyUIDeinit(void*);
void tc002FixtureDeinit(void* context) { onEasyUIDeinit(context); }
#elif defined(TC002_FIXTURE_DATA)
// Right name, wrong kind of symbol.
int onEasyUIDeinit = 0;
#endif
}
