#pragma once

#if defined(_WIN32)

#include <algorithm>
#include <array>
#include <cmath>
#include <windows.h>
#include <gdiplus.h>

namespace iris::vst3::editor {

// Static material symbols. These are interface illustrations, not measured
// waveforms or a physical simulation. The adjacent partials plot reads the
// actual modal-model helpers in IrisBody.h.
inline void drawBodyMaterialSymbol(Gdiplus::Graphics& g, int material,
                                  const Gdiplus::RectF& r, COLORREF accent,
                                  float strike=0.4f, float spread=0.25f) {
    const float x=r.X, y=r.Y, w=r.Width, h=r.Height;
    const bool compactSymbol=w<60.0f;
    auto color=[&](BYTE alpha) {
        return Gdiplus::Color(alpha,GetRValue(accent),GetGValue(accent),GetBValue(accent));
    };
    auto point=[&](float u,float v) { return Gdiplus::PointF(x+w*u,y+h*v); };
    Gdiplus::Pen outline(color(220),compactSymbol ? 1.1f : 1.5f);
    outline.SetStartCap(Gdiplus::LineCapRound); outline.SetEndCap(Gdiplus::LineCapRound);
    Gdiplus::Pen fine(color(105),compactSymbol ? .8f : 1.0f);
    Gdiplus::SolidBrush tint(color(16)), tint2(color(29)), solid(color(230));

    if (material==0) {
        const std::array<Gdiplus::PointF,6> rim {
            point(.22f,.13f), point(.64f,.02f), point(.87f,.30f),
            point(.77f,.82f), point(.34f,.98f), point(.09f,.61f)
        };
        const auto center=point(.46f+.07f*spread,.48f);
        g.FillPolygon(&tint,rim.data(),static_cast<INT>(rim.size()));
        const std::array<Gdiplus::PointF,3> facet {rim[1],rim[2],center};
        const std::array<Gdiplus::PointF,3> facet2 {rim[3],rim[4],center};
        g.FillPolygon(&tint2,facet.data(),3); g.FillPolygon(&tint2,facet2.data(),3);
        g.DrawPolygon(&outline,rim.data(),static_cast<INT>(rim.size()));
        for (const auto& vertex:rim) g.DrawLine(&fine,vertex,center);
        if (!compactSymbol) {
            g.DrawLine(&fine,rim[0],rim[4]); g.DrawLine(&fine,rim[1],rim[3]);
            for (const auto& vertex:rim) g.FillEllipse(&solid,vertex.X-2,vertex.Y-2,4.0f,4.0f);
        }
    } else if (material==1) {
        const auto state=g.Save();
        g.TranslateTransform(x+w*.5f,y+h*.51f); g.RotateTransform(-12.0f);
        for (int ring=0;ring<(compactSymbol ? 2 : 4);++ring) {
            const float inset=ring*(compactSymbol ? .18f : .09f);
            const Gdiplus::RectF ellipse(-w*(.43f-inset),-h*(.34f-inset*.55f),
                w*(.86f-2*inset),h*(.68f-inset*1.1f));
            if (ring==0) g.FillEllipse(&tint,ellipse);
            g.DrawEllipse(ring==0 ? &outline : &fine,ellipse);
        }
        const float a=(-.8f+strike*1.35f)*3.14159265f;
        const float px=w*.40f*std::cos(a), py=h*.31f*std::sin(a);
        const float radius=compactSymbol ? 1.6f : 3.3f;
        g.FillEllipse(&solid,px-radius,py-radius,2*radius,2*radius);
        g.Restore(state);
    } else if (material==2) {
        for (int bar=0;bar<3;++bar) {
            const float left=x+w*(.13f+.255f*bar), top=y+h*(.09f+.12f*bar);
            const float width=w*.18f, height=h*(.82f-.15f*bar);
            const Gdiplus::RectF plank(left,top,width,height);
            g.FillRectangle(&tint,plank); g.DrawRectangle(&outline,plank);
            if (!compactSymbol) for (int grain=0;grain<3;++grain) {
                std::array<Gdiplus::PointF,28> points{};
                for (int i=0;i<static_cast<int>(points.size());++i) {
                    const float t=i/static_cast<float>(points.size()-1);
                    points[i]=Gdiplus::PointF(left+width*(.24f+.25f*grain)+
                        1.5f*std::sin(t*7+bar+grain),top+6+t*(height-12));
                }
                g.DrawLines(&fine,points.data(),static_cast<INT>(points.size()));
            }
        }
    } else {
        const int count=compactSymbol ? 2 : 4;
        for (int string=0;string<count;++string) {
            const float center=y+h*(.2f+.2f*string);
            std::array<Gdiplus::PointF,96> points{};
            for (int i=0;i<static_cast<int>(points.size());++i) {
                const float t=i/static_cast<float>(points.size()-1);
                const float amplitude=(compactSymbol ? 1.4f : h*(.027f+.047f*strike))*(1.0f-.12f*string);
                points[i]=Gdiplus::PointF(x+w*(.06f+.88f*t),center+
                    amplitude*std::sin(t*3.14159265f)*std::sin(t*(2+string)*3.14159265f));
            }
            g.DrawLines(string==0 ? &outline : &fine,points.data(),static_cast<INT>(points.size()));
            const float radius=compactSymbol ? 1.0f : 2.5f;
            for (const float u:{.06f,.94f}) g.FillEllipse(&solid,x+w*u-radius,center-radius,2*radius,2*radius);
        }
    }
}

} // namespace iris::vst3::editor

#endif
