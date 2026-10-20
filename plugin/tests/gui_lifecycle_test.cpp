// Exercise the actual DLL's native editor without touching the user's DAW.
#define main iris_existing_host_main
#include "host_test.cpp"
#undef main
#include <chrono>
#include <thread>

namespace {
LONG CALLBACK failOnAccessViolation(EXCEPTION_POINTERS* exception) {
    if (exception->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION) {
        HMODULE module=nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(exception->ExceptionRecord->ExceptionAddress),&module);
        std::cerr<<"ACCESS_VIOLATION address="<<exception->ExceptionRecord->ExceptionAddress
            <<" module_offset=0x"<<std::hex
            <<(reinterpret_cast<uintptr_t>(exception->ExceptionRecord->ExceptionAddress)-reinterpret_cast<uintptr_t>(module))
            <<std::dec<<" read_write="<<exception->ExceptionRecord->ExceptionInformation[0]
            <<" target=0x"<<std::hex<<exception->ExceptionRecord->ExceptionInformation[1]<<std::dec<<std::endl;
        // A deliberate regression failure must not launch WER or take down FL.
        ExitProcess(77);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
void pump() { MSG m{}; while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&m);DispatchMessageW(&m);} }
}

int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    AddVectoredExceptionHandler(1,failOnAccessViolation);
    try {
        require(argc>=2,"gui_lifecycle_test <DLL> [seconds=0] [cycles=12]");
        const int seconds=argc>2?std::stoi(argv[2]):0;
        const int cycles=argc>3?std::stoi(argv[3]):12;
        Module module(argv[1]); Host host;
        Session session(module,host);
        for(int cycle=0;cycle<cycles;++cycle) {
            std::cout<<"OPEN "<<cycle<<std::endl;
            HWND parent=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"Iris isolated regression",
                WS_POPUP,-30000,-30000,1560,1140,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
            require(parent!=nullptr,"Parent window failed");
            ShowWindow(parent,SW_SHOWNOACTIVATE);
            IPlugView* view=session.controller->createView(ViewType::kEditor);
            require(view && view->attached(parent,kPlatformTypeHWND)==kResultOk,"Editor attach failed");
            auto child=FindWindowExW(parent,nullptr,L"IndependentAudioLab.IrisColor.Editor.v5",nullptr);
            require(child!=nullptr,"Native HWND missing");
            IPlugViewContentScaleSupport* scale=nullptr;
            require(view->queryInterface(IPlugViewContentScaleSupport::iid,reinterpret_cast<void**>(&scale))==kResultOk,"Scale interface missing");
            for(float factor:{1.f,1.25f,1.5f}) {
                require(scale->setContentScaleFactor(factor)==kResultOk,"Scale rejected");
                InvalidateRect(child,nullptr,FALSE); SendMessageW(child,WM_PAINT,0,0); pump();
            }
            scale->release();
            const auto start=std::chrono::steady_clock::now();
            while(cycle==0 && std::chrono::steady_clock::now()-start<std::chrono::seconds(seconds)) {
                pump(); InvalidateRect(child,nullptr,FALSE); SendMessageW(child,WM_PAINT,0,0);
                std::this_thread::sleep_for(std::chrono::milliseconds(33));
            }
            require(view->removed()==kResultOk,"Remove failed");
            DestroyWindow(parent); view->release(); pump();
            std::cout<<"CLOSE "<<cycle<<std::endl;
        }
        pass("native GUI idle, repaint, scale and repeated attach/remove without audio");
        return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
}
