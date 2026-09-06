#include "pin.H"
#include "sde-init.H"
#include "sde-pinplay-supp.H"
#include "pinplay.H"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>

static PINPLAY_ENGINE *engine = nullptr;
static ADDRINT known_code_addr = 0;
static std::mutex output_mutex;
static std::ofstream output;

static ADDRINT logical_rsp(const CONTEXT *context) {
#if defined(TARGET_IA32E)
    return PIN_GetContextReg(context, REG_RSP);
#else
    return PIN_GetContextReg(context, REG_ESP);
#endif
}

static void probe(THREADID pin_tid, CONTEXT *context, int worker_id,
                  ADDRINT worker0_stack, ADDRINT worker1_stack, ADDRINT shared_addr) {
    ADDRINT rsp = logical_rsp(context);
    const ADDRINT addresses[] = {known_code_addr, shared_addr, worker0_stack, worker1_stack, rsp};
    const char *categories[] = {"code", "global", "worker0_stack", "worker1_stack", "caller_rsp"};

    std::lock_guard<std::mutex> lock(output_mutex);
    for (unsigned i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
        ADDRINT translated = engine->ReplayerTranslateAddress(addresses[i]);
        output << worker_id << ' ' << pin_tid << ' ' << categories[i]
               << " logical=0x" << std::hex << addresses[i]
               << " translated=0x" << translated
               << " current_rsp=0x" << rsp
               << " translated_rsp=0x" << engine->ReplayerTranslateAddress(rsp)
               << std::dec << '\n';
    }
    output.flush();
}

static void image(IMG img, VOID *) {
    RTN known_code = RTN_FindByName(img, "known_code");
    if (RTN_Valid(known_code)) {
        known_code_addr = RTN_Address(known_code);
    }

    RTN replay_point = RTN_FindByName(img, "replay_point");
    if (RTN_Valid(replay_point)) {
        RTN_Open(replay_point);
        RTN_InsertCall(replay_point, IPOINT_BEFORE, (AFUNPTR)probe,
                       IARG_THREAD_ID, IARG_CONTEXT,
                       IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                       IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
                       IARG_FUNCARG_ENTRYPOINT_VALUE, 2,
                       IARG_FUNCARG_ENTRYPOINT_VALUE, 3,
                       IARG_END);
        RTN_Close(replay_point);
    }
}

int main(int argc, char **argv) {
    PIN_InitSymbols();
    sde_pin_init(argc, argv);
    sde_tracing_flags(argc, argv);
    sde_init();
    sde_tracing_activate(argc, argv);
    engine = sde_tracing_get_pinplay_engine();

    const char *path = std::getenv("MT_PROBE_OUT");
    output.open(path ? path : "mt_address_translation.out");
    output << "# worker pin_tid category logical translated current_rsp translated_rsp\n";
    IMG_AddInstrumentFunction(image, nullptr);
    PIN_StartProgram();
    return 0;
}
