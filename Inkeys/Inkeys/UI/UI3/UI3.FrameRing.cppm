module;
#include <windows.h>
#include <d2d1_1.h>
#include <wrl/client.h>
#include <algorithm>
#include <utility>

// L2 按钮沿框进度环；共用加载反馈样式，资源由控件持有。
export module Inkeys.UI.UI3.FrameRing;

import Inkeys.UI.UI3.LoadingIndicator;

export namespace Inkeys::UI::UI3
{
    // 沿按钮圆角边框内侧的环：页码加载环与长按进度环共用路径、线宽与圆头描边；无轨道。
    // 路径只在尺寸/圆角变化时重建，设备丢失时由所有者 Reset。
    struct ButtonFrameRing
    {
        struct Key{float width=0,height=0,radius=0;bool operator==(const Key&) const noexcept=default;};
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
        Key key;float perimeter=0;
        void Reset() noexcept{path.Reset();brush.Reset();key={};perimeter=0;}
        struct Arc{double start=0,length=0;bool progress=false;};
        // 按钮位置/尺寸/圆角/边框为 DIP，unit 为面板姿态缩放；arc 以周长比例表示，起点为顶边中点、顺时针。
        // progress=true 时弧长可到整圈（进度环），否则保留页码环原有的最大弧长。
        // 返回 false 表示资源不可用，调用方据此停止尝试；尺寸过小仅跳过绘制。
        bool Draw(ID2D1DeviceContext* context,double left,double top,double buttonWidth,double buttonHeight,
            float corner,float frameThickness,float unit,float zoom,Arc arc,COLORREF color,float opacity)
        {
            using namespace PageLoadingStyle;
            const float inset=(frameThickness+Gap+Width/2)*unit,weight=Width*unit;
            const float width=static_cast<float>(buttonWidth)-2*inset,height=static_cast<float>(buttonHeight)-2*inset;
            if(width<=weight||height<=weight)return true;
            // 圆角与按钮自身圆角一致（环中心线取按钮圆角），仅在放不下时收小。
            const float radius=(std::max)(.5F*unit,(std::min)({corner,width/2,height/2}));
            const Key next{width,height,radius};
            Microsoft::WRL::ComPtr<ID2D1Factory> factory;context->GetFactory(factory.GetAddressOf());
            if(!factory)return false;
            if(!path||!(next==key)){
                path.Reset();perimeter=0;
                Microsoft::WRL::ComPtr<ID2D1PathGeometry> geometry;Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
                HRESULT hr=factory->CreatePathGeometry(geometry.GetAddressOf());
                if(SUCCEEDED(hr))hr=geometry->Open(sink.GetAddressOf());
                if(SUCCEEDED(hr)){
                    const auto point=[](float x,float y){return D2D1::Point2F(x,y);};
                    const auto corner90=[&](float x,float y){sink->AddArc(D2D1::ArcSegment(point(x,y),D2D1::SizeF(radius,radius),0,D2D1_SWEEP_DIRECTION_CLOCKWISE,D2D1_ARC_SIZE_SMALL));};
                    // 起点固定为顶边中点，顺时针一圈。
                    sink->BeginFigure(point(width/2,0),D2D1_FIGURE_BEGIN_HOLLOW);
                    sink->AddLine(point(width-radius,0));corner90(width,radius);
                    sink->AddLine(point(width,height-radius));corner90(width-radius,height);
                    sink->AddLine(point(radius,height));corner90(0,height-radius);
                    sink->AddLine(point(0,radius));corner90(radius,0);
                    sink->AddLine(point(width/2,0));
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);hr=sink->Close();
                }
                float total=0;
                if(SUCCEEDED(hr))hr=geometry->ComputeLength(nullptr,D2D1_DEFAULT_FLATTENING_TOLERANCE,&total);
                if(SUCCEEDED(hr)&&!brush)hr=context->CreateSolidColorBrush(D2D1::ColorF(0,0,0,0),brush.GetAddressOf());
                if(FAILED(hr)||total<=weight*3)return false;
                path=std::move(geometry);key=next;perimeter=total;
            }
            // 圆角端帽会向两端各延伸半个线宽，扣除后弧段的可见总长才等于 arc.length。
            const float visible=(std::clamp)(static_cast<float>(arc.length)*perimeter,weight,arc.progress?perimeter:perimeter-3*weight);
            const float dashes[2]={(visible-weight)/weight,(perimeter-(visible-weight))/weight};
            const float offset=(perimeter-static_cast<float>(arc.start)*perimeter)/weight;
            Microsoft::WRL::ComPtr<ID2D1StrokeStyle> stroke;
            const auto properties=D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND,D2D1_CAP_STYLE_ROUND,
                D2D1_CAP_STYLE_ROUND,D2D1_LINE_JOIN_ROUND,10.0F,D2D1_DASH_STYLE_CUSTOM,offset);
            if(FAILED(factory->CreateStrokeStyle(properties,dashes,2,stroke.GetAddressOf())))return true;
            brush->SetColor(D2D1::ColorF(GetRValue(color)/255.0F,GetGValue(color)/255.0F,GetBValue(color)/255.0F,opacity));
            D2D1_MATRIX_3X2_F original;context->GetTransform(&original);
            context->SetTransform(D2D1::Matrix3x2F::Scale(zoom,zoom)*D2D1::Matrix3x2F::Translation(static_cast<FLOAT>((left+inset)*zoom),static_cast<FLOAT>((top+inset)*zoom))*original);
            context->DrawGeometry(path.Get(),brush.Get(),weight,stroke.Get());
            context->SetTransform(original);return true;
        }
    };
}
