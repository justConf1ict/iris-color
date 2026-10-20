#pragma once

#if defined(_WIN32)

#include <algorithm>
#include <array>
#include <cwchar>
#include <memory>
#include <string>
#include <vector>

namespace iris::vst3::editor {

// The bundled faces are private to this editor. They are never installed in
// Windows, and all GDI+ objects must be released before GdiplusShutdown.
class PrivateTypography {
public:
    static constexpr std::array<float,10> sizes {
        14.5f, 13.0f, 29.0f, 17.0f, 15.0f,
        29.0f, 44.0f, 13.0f, 14.0f, 21.0f
    };

    void initialize(HINSTANCE module, const wchar_t* fallback) {
        clear();
        const auto folder=resourceFolder(module);
        if (!folder.empty()) {
            collection_=std::make_unique<Gdiplus::PrivateFontCollection>();
            collection_->AddFontFile((folder+L"DMSans-Regular.ttf").c_str());
            collection_->AddFontFile((folder+L"DMSans-Medium.ttf").c_str());
            const int count=collection_->GetFamilyCount();
            if (count>0) {
                std::vector<Gdiplus::FontFamily> families(static_cast<std::size_t>(count));
                int found=0;
                if (collection_->GetFamilies(count,families.data(),&found)==Gdiplus::Ok) {
                    for (int i=0;i<found;++i) {
                        wchar_t name[LF_FACESIZE]{};
                        if (families[i].GetFamilyName(name)!=Gdiplus::Ok ||
                            !families[i].IsStyleAvailable(Gdiplus::FontStyleRegular)) continue;
                        // Use the enumerated family itself. Some GDI+ backends
                        // cannot resolve a valid private family again by name.
                        if (std::wcscmp(name,L"DM Sans")==0) regular_.reset(families[i].Clone());
                        else if (std::wcscmp(name,L"DM Sans Medium")==0) medium_.reset(families[i].Clone());
                    }
                }
            }
        }
        for (std::size_t i=0;i<faces_.size();++i) {
            const bool emphasis=i==2 || i==4 || i==8;
            const auto* family=emphasis && medium_ ? medium_.get() : regular_.get();
            if (family) faces_[i]=std::make_unique<Gdiplus::Font>(
                family,sizes[i],Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);
            if (!faces_[i] || faces_[i]->GetLastStatus()!=Gdiplus::Ok)
                faces_[i]=std::make_unique<Gdiplus::Font>(
                    fallback,sizes[i],Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);
            // A missing system family must leave the GDI text path reachable.
            if (faces_[i]->GetLastStatus()!=Gdiplus::Ok) faces_[i].reset();
        }
    }

    const Gdiplus::Font* face(std::size_t index) const {
        return index<faces_.size() ? faces_[index].get() : nullptr;
    }

    void clear() {
        for (auto& face:faces_) face.reset();
        medium_.reset();
        regular_.reset();
        collection_.reset();
    }

private:
    static std::wstring resourceFolder(HINSTANCE module) {
        std::vector<wchar_t> path(1024);
        for (;;) {
            const DWORD count=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));
            if (!count) return {};
            if (count<path.size()-1) {
                std::wstring file(path.data(),count);
                const auto binary=file.find_last_of(L"\\/");
                if (binary==std::wstring::npos) return {};
                file.resize(binary);
                const auto architecture=file.find_last_of(L"\\/");
                if (architecture==std::wstring::npos) return {};
                file.resize(architecture);
                return file+L"\\Resources\\fonts\\";
            }
            if (path.size()>=32768) return {};
            path.resize(std::min<std::size_t>(32768,path.size()*2));
        }
    }

    std::unique_ptr<Gdiplus::PrivateFontCollection> collection_;
    std::unique_ptr<Gdiplus::FontFamily> regular_, medium_;
    std::array<std::unique_ptr<Gdiplus::Font>,10> faces_{};
};

} // namespace iris::vst3::editor
#endif
