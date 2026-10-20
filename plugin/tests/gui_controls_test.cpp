// Target only the test's own HWNDs. No global keyboard/mouse injection.
#define main iris_existing_host_main
#include "host_test.cpp"
#undef main
struct TestEditor {
    Session& session;
    HWND parent=nullptr,child=nullptr;
    IPlugView* view=nullptr;
    explicit TestEditor(Session& s):session(s) {
        parent=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"Iris control regression",WS_POPUP,
            -30000,-30000,1560,1140,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(parent!=nullptr,"Parent failed");ShowWindow(parent,SW_SHOWNOACTIVATE);
        view=session.controller->createView(ViewType::kEditor);
        require(view && view->attached(parent,kPlatformTypeHWND)==kResultOk,"Editor attach failed");
        child=FindWindowExW(parent,nullptr,L"IndependentAudioLab.IrisColor.Editor.v5",nullptr);
        require(child!=nullptr,"Editor child missing");
    }
    void paint() {InvalidateRect(child,nullptr,FALSE);SendMessageW(child,WM_PAINT,0,0);}
    void click(float factor,int x,int y) {
        const auto position=MAKELPARAM(static_cast<int>(std::lround(x*factor)),static_cast<int>(std::lround(y*factor)));
        SendMessageW(child,WM_LBUTTONDOWN,MK_LBUTTON,position);SendMessageW(child,WM_LBUTTONUP,0,position);paint();
    }
    void exercise() {
        IPlugViewContentScaleSupport* scaled=nullptr;
        require(view->queryInterface(IPlugViewContentScaleSupport::iid,reinterpret_cast<void**>(&scaled))==kResultOk,"Scale missing");
        for(float factor:{1.f,1.25f,1.5f}) {
            require(scaled->setContentScaleFactor(factor)==kResultOk,"Scale failed");
            for(int tab:{403,474,550,655})click(factor,tab,45);
            click(factor,474,45);
            for(int m=0;m<4;++m) {
                click(factor,500+141*m,129);
                require(discrete(kBodyMaterial,session.controller->getParamNormalized(kBodyMaterial))==m,"Material tile hit test failed");
            }
            SendMessageW(child,WM_KEYDOWN,VK_HOME,0);
            require(discrete(kBodyMaterial,session.controller->getParamNormalized(kBodyMaterial))==0,"Home key failed");
            SendMessageW(child,WM_KEYDOWN,VK_END,0);
            require(discrete(kBodyMaterial,session.controller->getParamNormalized(kBodyMaterial))==3,"End key failed");
            const auto before=session.controller->getParamNormalized(kBypass);
            click(factor,965,45);
            require(session.controller->getParamNormalized(kBypass)!=before,"Bypass click failed");
            click(factor,965,45);paint();
        }
        scaled->release();
    }
    void close(bool hostDestroysParentFirst) {
        if(!view)return;
        if(hostDestroysParentFirst){DestroyWindow(parent);parent=nullptr;}
        require(view->removed()==kResultOk,"Remove failed");view->release();view=nullptr;
        if(parent)DestroyWindow(parent);parent=child=nullptr;
    }
    ~TestEditor(){close(false);}
};
int main(int argc,char** argv) {
    try {
        require(argc==2,"gui_controls_test <DLL>");Module module(argv[1]);Host host;
        DWORD baselineGdi=0,baselineUser=0;
        for(int cycle=0;cycle<12;++cycle) {
            Session one(module,host),two(module,host);
            TestEditor first(one),second(two);first.exercise();second.exercise();
            first.close(cycle%2==0);second.exercise();second.close(cycle%2!=0);
            MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
            const auto gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),user=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
            if(cycle==0){baselineGdi=gdi;baselineUser=user;}
            require(gdi<=baselineGdi+8 && user<=baselineUser+8,"GUI resources grow across editor cycles");
            std::cout<<"PAIR PASS "<<cycle<<" GDI="<<gdi<<" USER="<<user<<std::endl;
        }
        pass("four pages/materials, bypass, 100/125/150% input, concurrent editors, parent-first destruction, bounded handles");return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
}
