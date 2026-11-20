module;

// 通用 UI3 聚合入口：基础绘制与动效、基础控件、序列与面板、宿主与呈现四层责任。
// 依赖仅由产品层向四层及基础设施推进，不得依赖产品模块。
export module Inkeys.UI.UI3;

export import :Theme;
export import :Primitive;
export import :Format;
export import :Envelope;
export import :Lighting;
export import :Renderer;
export import :Button;
export import :VisualDirtySampler;
export import :List;
export import :Panel;
export import :ColorPicker;
export import :Host;
export import :HostInput;

export import Inkeys.UI.UI3.Animation;
export import Inkeys.UI.UI3.Metrics;
export import Inkeys.UI.UI3.WakeSignal;
export import Inkeys.UI.UI3.FramePacing;
export import Inkeys.UI.UI3.DragSession;
export import Inkeys.UI.UI3.Material;
export import Inkeys.UI.UI3.DirtyRegion;
export import Inkeys.UI.UI3.VisualDirty;
export import Inkeys.UI.UI3.Superellipse;
export import Inkeys.UI.UI3.LoadingIndicator;
export import Inkeys.UI.UI3.FrameRing;
export import Inkeys.UI.UI3.PresentationProbe;
export import Inkeys.UI.UI3.Interaction;
export import Inkeys.UI.UI3.Progress;
