// 无注册、无窗口的 IDispatch 图；所有 Office 类型接口故意拒绝。
#include <windows.h>
#include <oaidl.h>
#include <ocidl.h>
#include <olectl.h>
#include <oleauto.h>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <new>

namespace {
enum NodeId : UINT { Application, Presentation, Window, View, Slides,
    Slide1, Slide2, Slide3, Navigation, ShowWindows, NodeCount };
enum MemberId : DISPID { Item = 0, Name = 1, Caption, FullName, SlidesProperty,
    Count, ViewProperty = 7, Slide, SlideIndex, SlideID, State, Hwnd,
    Next, Previous, Exit, Activate, ActivePresentation, SlideShowWindow,
    SlideShowWindows, PresentationProperty, Active, SlideNavigation,
    PointerType, LaserPointerEnabled, Visible, ApplicationProperty };
enum Profile : UINT { WithTypeInfo, NoTypeInfo, TypeInfoFailure, WithEvents };

// 全部是定宽纯值；字段顺序与 C# Pack=8 一致，禁止把 COM 指针送入账本。
struct NodeLedger {
    int64_t references, created, destroyed, queries, rejectedQueries,
        typedRejected, addRefs, releases, invokes, nameLookups,
        typeInfoCounts, typeInfoGets, returnedObjects, faults,
        nextCalls, previousCalls, exitCalls, activateCalls, lastHresult;
};
struct Ledger {
    UINT version, nodeCount;
    int64_t liveObjects, references, ownerReleased;
};
struct EventLedger {
    int64_t advises, unadvises, activeConnections, sinkQueries, callbacks,
        sinkAddRefs, sinkReleases, containerQueries, pointAcquisitions,
        emittedArguments, clearedArguments, lastInvokeHresult,
        lastUnadviseHresult, lastAdviseHresult, lastCancel;
};
static_assert(sizeof(NodeLedger) == 152 && sizeof(Ledger) == 32, "fixture ABI");
static_assert(sizeof(EventLedger) == 120, "event fixture ABI");

const IID PowerPointWindowIid = { 0x91493453, 0x5a91, 0x11cf,
    { 0x87, 0x00, 0x00, 0xaa, 0x00, 0x60, 0x26, 0x3b } };
const IID PowerPointEventsIid = { 0x914934c2, 0x5a91, 0x11cf,
    { 0x87, 0x00, 0x00, 0xaa, 0x00, 0x60, 0x26, 0x3b } };

struct Member {
    DISPID id;
    const wchar_t* name;
    INVOKEKIND kind;
    VARTYPE result;
    bool indexed;
};
const Member Members[] = {
    { Item, L"Item", INVOKE_PROPERTYGET, VT_DISPATCH, true },
    { Name, L"Name", INVOKE_PROPERTYGET, VT_BSTR, false },
    { Caption, L"Caption", INVOKE_PROPERTYGET, VT_BSTR, false },
    { FullName, L"FullName", INVOKE_PROPERTYGET, VT_BSTR, false },
    { SlidesProperty, L"Slides", INVOKE_PROPERTYGET, VT_DISPATCH, false },
    { Count, L"Count", INVOKE_PROPERTYGET, VT_I4, false },
    { ViewProperty, L"View", INVOKE_PROPERTYGET, VT_DISPATCH, false },
    { Slide, L"Slide", INVOKE_PROPERTYGET, VT_DISPATCH, false },
    { SlideIndex, L"SlideIndex", INVOKE_PROPERTYGET, VT_I4, false },
    { SlideID, L"SlideID", INVOKE_PROPERTYGET, VT_I4, false },
    { State, L"State", INVOKE_PROPERTYGET, VT_I4, false },
    { Hwnd, L"HWND", INVOKE_PROPERTYGET, VT_I4, false },
    { Next, L"Next", INVOKE_FUNC, VT_EMPTY, false },
    { Previous, L"Previous", INVOKE_FUNC, VT_EMPTY, false },
    { Exit, L"Exit", INVOKE_FUNC, VT_EMPTY, false },
    { Activate, L"Activate", INVOKE_FUNC, VT_EMPTY, false },
    { ActivePresentation, L"ActivePresentation", INVOKE_PROPERTYGET, VT_DISPATCH, false },
    { SlideShowWindow, L"SlideShowWindow", INVOKE_PROPERTYGET, VT_DISPATCH, false },
    { SlideShowWindows, L"SlideShowWindows", INVOKE_PROPERTYGET, VT_DISPATCH, false },
    { PresentationProperty, L"Presentation", INVOKE_PROPERTYGET, VT_DISPATCH, false },
    { Active, L"Active", INVOKE_PROPERTYGET, VT_I4, false },
    { SlideNavigation, L"SlideNavigation", INVOKE_PROPERTYGET, VT_DISPATCH, false },
    { PointerType, L"PointerType", INVOKE_PROPERTYGET, VT_I4, false },
    { LaserPointerEnabled, L"LaserPointerEnabled", INVOKE_PROPERTYGET, VT_BOOL, false },
    { Visible, L"Visible", INVOKE_PROPERTYGET, VT_BOOL, false },
    { ApplicationProperty, L"Application", INVOKE_PROPERTYGET, VT_DISPATCH, false }
};

bool Supports(UINT node, DISPID member) {
    switch (node) {
    case Application: return member == Name || member == Caption ||
        member == ActivePresentation || member == SlideShowWindows;
    case Presentation: return member == Name || member == FullName ||
        member == SlidesProperty || member == SlideShowWindow || member == ApplicationProperty;
    case Window: return member == ViewProperty || member == Hwnd || member == Active ||
        member == PresentationProperty || member == Activate || member == SlideNavigation;
    case View: return member == Slide || member == State || member == Next ||
        member == Previous || member == Exit || member == PointerType || member == LaserPointerEnabled;
    case Slides: return member == Count || member == Item;
    case Slide1: case Slide2: case Slide3: return member == SlideIndex || member == SlideID;
    case Navigation: return member == Visible;
    case ShowWindows: return member == Count || member == Item;
    default: return false;
    }
}

HRESULT MakeTypeInfo(UINT fixtureId, UINT node, ITypeInfo** result) {
    *result = nullptr;
    ICreateTypeLib2* library = nullptr;
    ICreateTypeInfo* create = nullptr;
    // 只在内存 LayOut；不 SaveAllChanges，也不注册类型库。
    HRESULT hr = CreateTypeLib2(sizeof(void*) == 8 ? SYS_WIN64 : SYS_WIN32,
        L"InkeysDispatchFixture.memory", &library);
    if (FAILED(hr)) return hr;
    GUID guid = { 0x6c480000U + fixtureId, 0x917a, static_cast<USHORT>(node),
        { 0x9f, 0xa2, 0x71, 0x42, 0xa1, 0x0f, 0x58, 0x3e } };
    hr = library->SetGuid(guid);
    if (SUCCEEDED(hr)) hr = library->SetVersion(1, 0);
    if (SUCCEEDED(hr)) hr = library->CreateTypeInfo(L"FixtureDispatch", TKIND_DISPATCH, &create);
    guid.Data4[7] = 0x3f;
    if (SUCCEEDED(hr)) hr = create->SetGuid(guid);
    // FOLEAUTOMATION 只适合类型接口；手写 dispatch 元数据不设置该标志。
    if (SUCCEEDED(hr)) hr = create->SetTypeFlags(TYPEFLAG_FDISPATCHABLE);
    UINT index = 0;
    for (const auto& member : Members) {
        if (FAILED(hr) || !Supports(node, member.id)) continue;
        ELEMDESC parameter = {};
        parameter.tdesc.vt = VT_I4;
        parameter.paramdesc.wParamFlags = PARAMFLAG_FIN;
        FUNCDESC function = {};
        function.memid = member.id;
        function.funckind = FUNC_DISPATCH;
        function.invkind = member.kind;
        function.callconv = CC_STDCALL;
        function.cParams = member.indexed ? 1 : 0;
        function.lprgelemdescParam = member.indexed ? &parameter : nullptr;
        function.elemdescFunc.tdesc.vt = member.result;
        // 模拟 PIA 中的 HWND restricted 标记，不扩大到其他成员。
        function.wFuncFlags = member.id == Hwnd ? FUNCFLAG_FRESTRICTED : 0;
        hr = create->AddFuncDesc(index, &function);
        LPOLESTR names[] = { const_cast<LPOLESTR>(member.name), const_cast<LPOLESTR>(L"index") };
        if (SUCCEEDED(hr)) hr = create->SetFuncAndParamNames(index, names, member.indexed ? 2 : 1);
        ++index;
        if (member.id == PointerType || member.id == LaserPointerEnabled || member.id == Visible) {
            parameter.tdesc.vt = member.result;
            function.invkind = INVOKE_PROPERTYPUT;
            function.cParams = 1;
            function.lprgelemdescParam = &parameter;
            function.elemdescFunc.tdesc.vt = VT_VOID;
            if (SUCCEEDED(hr)) hr = create->AddFuncDesc(index, &function);
            // 同一 property 只设置一次名称；put 的最后一个 value 参数没有名称。
            ++index;
        }
    }
    if (SUCCEEDED(hr)) hr = create->LayOut();
    if (SUCCEEDED(hr)) hr = create->QueryInterface(IID_ITypeInfo, reinterpret_cast<void**>(result));
    if (create) create->Release();
    library->Release();
    return hr;
}

class DispatchNode;
struct Fixture {
    UINT id, profile;
    std::mutex gate;
    NodeLedger ledger[NodeCount] = {};
    DispatchNode* nodes[NodeCount] = {};
    bool released = false;
    UINT faultNode = NodeCount;
    DISPID faultMember = DISPID_UNKNOWN;
    HRESULT faultHresult = S_OK;
    UINT faultOccurrence = 1, faultSeen = 0;
    LONG currentPage = 2, state = 1, pointerType = 1;
    VARIANT_BOOL laser = VARIANT_FALSE, visible = VARIANT_FALSE;
    EventLedger events = {};
    IDispatch* sink = nullptr;
    DWORD cookie = 0, nextCookie = 0;
    UINT eventFaultOperation = 0, eventFaultOccurrence = 0, eventFaultSeen = 0;
    HRESULT eventFault = S_OK;

    Fixture(UINT idValue, UINT profileValue) : id(idValue), profile(profileValue) {}
    ~Fixture() { DropOwner(); }
    void DropOwner();
};

class DispatchNode final : public IDispatch, public IConnectionPointContainer, public IConnectionPoint {
    LONG references_ = 1;
    Fixture* fixture_;
    UINT node_;
    ITypeInfo* typeInfo_ = nullptr;

    HRESULT Object(UINT node, VARIANT* result) {
        DispatchNode* child = fixture_->nodes[node];
        if (!child) return RPC_E_DISCONNECTED;
        child->AddRef();
        {
            std::lock_guard<std::mutex> lock(fixture_->gate);
            ++fixture_->ledger[node_].returnedObjects;
        }
        result->vt = VT_DISPATCH;
        result->pdispVal = child;
        return S_OK;
    }

    HRESULT InvokeValue(DISPID id, WORD flags, DISPPARAMS* arguments, VARIANT* result) {
        const bool put = (flags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) != 0;
        if (put) {
            if (!arguments || arguments->cArgs != 1) return DISP_E_BADPARAMCOUNT;
            VARIANT value;
            VariantInit(&value);
            HRESULT hr = VariantChangeType(&value, &arguments->rgvarg[0], 0,
                static_cast<VARTYPE>(id == PointerType ? VT_I4 : VT_BOOL));
            if (FAILED(hr)) return hr;
            if (id == PointerType) fixture_->pointerType = value.lVal;
            else if (id == LaserPointerEnabled) fixture_->laser = value.boolVal;
            else if (id == Visible) fixture_->visible = value.boolVal;
            else { VariantClear(&value); return DISP_E_MEMBERNOTFOUND; }
            VariantClear(&value);
            return S_OK;
        }
        if (id == Item) {
            if (!arguments || arguments->cArgs != 1) return DISP_E_BADPARAMCOUNT;
            VARIANT index;
            VariantInit(&index);
            HRESULT hr = VariantChangeType(&index, &arguments->rgvarg[0], 0, VT_I4);
            if (FAILED(hr)) return hr;
            LONG ordinal = index.lVal;
            VariantClear(&index);
            if (node_ == ShowWindows) return ordinal == 1 ? Object(Window, result) : DISP_E_BADINDEX;
            if (ordinal < 1 || ordinal > 3) return DISP_E_BADINDEX;
            return Object(Slide1 + ordinal - 1, result);
        }
        if (arguments && arguments->cArgs != 0) return DISP_E_BADPARAMCOUNT;
        switch (id) {
        case Name: case Caption: case FullName:
            result->vt = VT_BSTR;
            result->bstrVal = SysAllocString(node_ == Application ? L"Microsoft PowerPoint Fixture" :
                id == FullName ? L"C:\\Fixture\\Deck.pptx" : L"Deck.pptx");
            return result->bstrVal ? S_OK : E_OUTOFMEMORY;
        case ApplicationProperty: return Object(Application, result);
        case ActivePresentation: case PresentationProperty: return Object(Presentation, result);
        case SlideShowWindow: return Object(Window, result);
        case SlideShowWindows: return Object(ShowWindows, result);
        case SlidesProperty: return Object(Slides, result);
        case ViewProperty: return Object(View, result);
        case SlideNavigation: return Object(Navigation, result);
        case Slide: return Object(Slide1 + fixture_->currentPage - 1, result);
        case Next: case Previous: case Exit: case Activate: {
            std::lock_guard<std::mutex> lock(fixture_->gate);
            NodeLedger& stat = fixture_->ledger[node_];
            if (id == Next) { ++stat.nextCalls; if (fixture_->currentPage < 3) ++fixture_->currentPage; }
            if (id == Previous) { ++stat.previousCalls; if (fixture_->currentPage > 1) --fixture_->currentPage; }
            if (id == Exit) { ++stat.exitCalls; fixture_->state = 5; }
            if (id == Activate) ++stat.activateCalls;
            return S_OK;
        }
        case LaserPointerEnabled: case Visible:
            result->vt = VT_BOOL;
            result->boolVal = id == Visible ? fixture_->visible : fixture_->laser;
            return S_OK;
        default:
            result->vt = VT_I4;
            switch (id) {
            case Count: result->lVal = node_ == ShowWindows ? 1 : 3; break;
            case SlideIndex: result->lVal = node_ - Slide1 + 1; break;
            case SlideID: result->lVal = (node_ - Slide1 + 1) * 101; break;
            case State: result->lVal = fixture_->state; break;
            case Hwnd: result->lVal = 0; break;
            case Active: result->lVal = -1; break;
            case PointerType: result->lVal = fixture_->pointerType; break;
            default: return DISP_E_MEMBERNOTFOUND;
            }
            return S_OK;
        }
    }

public:
    DispatchNode(Fixture* fixture, UINT node) : fixture_(fixture), node_(node) {
        fixture_->ledger[node_].references = 1;
        fixture_->ledger[node_].created = 1;
    }
    ~DispatchNode() { if (typeInfo_) typeInfo_->Release(); }
    HRESULT Initialize() {
        return fixture_->profile == WithTypeInfo || fixture_->profile == WithEvents ?
            MakeTypeInfo(fixture_->id, node_, &typeInfo_) : S_OK;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        const bool eventContainer = node_ == Application && fixture_->profile == WithEvents &&
            iid == IID_IConnectionPointContainer;
        const bool eventPoint = node_ == Application && fixture_->profile == WithEvents && iid == IID_IConnectionPoint;
        const bool accepted = iid == IID_IUnknown || iid == IID_IDispatch || eventContainer || eventPoint;
        {
            std::lock_guard<std::mutex> lock(fixture_->gate);
            ++fixture_->ledger[node_].queries;
            if (!accepted) ++fixture_->ledger[node_].rejectedQueries;
            if (iid == PowerPointWindowIid) ++fixture_->ledger[node_].typedRejected;
            if (eventContainer) ++fixture_->events.containerQueries;
        }
        if (!accepted) return E_NOINTERFACE;
        if (eventContainer) *value = static_cast<IConnectionPointContainer*>(this);
        else if (eventPoint) *value = static_cast<IConnectionPoint*>(this);
        else *value = static_cast<IDispatch*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        LONG value = InterlockedIncrement(&references_);
        std::lock_guard<std::mutex> lock(fixture_->gate);
        ++fixture_->ledger[node_].addRefs;
        fixture_->ledger[node_].references = value;
        return value;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG value = InterlockedDecrement(&references_);
        {
            std::lock_guard<std::mutex> lock(fixture_->gate);
            ++fixture_->ledger[node_].releases;
            fixture_->ledger[node_].references = value;
            if (value == 0) ++fixture_->ledger[node_].destroyed;
        }
        if (value == 0) delete this;
        return value;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* count) override {
        if (!count) return E_POINTER;
        std::lock_guard<std::mutex> lock(fixture_->gate);
        ++fixture_->ledger[node_].typeInfoCounts;
        *count = fixture_->profile == NoTypeInfo ? 0 : 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT index, LCID, ITypeInfo** info) override {
        if (!info) return E_POINTER;
        *info = nullptr;
        std::lock_guard<std::mutex> lock(fixture_->gate);
        ++fixture_->ledger[node_].typeInfoGets;
        if (index != 0 || fixture_->profile == NoTypeInfo) return DISP_E_BADINDEX;
        if (fixture_->profile == TypeInfoFailure) return TYPE_E_CANTLOADLIBRARY;
        typeInfo_->AddRef();
        *info = typeInfo_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID iid, LPOLESTR* names, UINT count,
        LCID, DISPID* ids) override {
        if (iid != IID_NULL) return DISP_E_UNKNOWNINTERFACE;
        if (!names || !ids || !count) return E_INVALIDARG;
        {
            std::lock_guard<std::mutex> lock(fixture_->gate);
            ++fixture_->ledger[node_].nameLookups;
        }
        for (UINT i = 0; i < count; ++i) {
            ids[i] = DISPID_UNKNOWN;
            for (const auto& member : Members) {
                if (Supports(node_, member.id) && _wcsicmp(names[i], member.name) == 0) {
                    ids[i] = member.id;
                    break;
                }
            }
            if (ids[i] == DISPID_UNKNOWN) return DISP_E_UNKNOWNNAME;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID id, REFIID iid, LCID, WORD flags,
        DISPPARAMS* arguments, VARIANT* result, EXCEPINFO*, UINT*) override {
        if (iid != IID_NULL) return DISP_E_UNKNOWNINTERFACE;
        VARIANT ignored;
        VariantInit(&ignored);
        if (!result) result = &ignored;
        VariantInit(result);
        HRESULT hr = S_OK;
        {
            std::lock_guard<std::mutex> lock(fixture_->gate);
            ++fixture_->ledger[node_].invokes;
            if (fixture_->released) hr = RPC_E_DISCONNECTED;
            if (fixture_->faultNode == node_ && fixture_->faultMember == id) {
                ++fixture_->faultSeen;
                if (fixture_->faultOccurrence == 0 || fixture_->faultSeen == fixture_->faultOccurrence) {
                    hr = fixture_->faultHresult;
                    ++fixture_->ledger[node_].faults;
                }
            }
        }
        if (SUCCEEDED(hr)) hr = Supports(node_, id) ? InvokeValue(id, flags, arguments, result) : DISP_E_MEMBERNOTFOUND;
        {
            std::lock_guard<std::mutex> lock(fixture_->gate);
            fixture_->ledger[node_].lastHresult = hr;
        }
        VariantClear(&ignored);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE EnumConnectionPoints(IEnumConnectionPoints** result) override {
        if (result) *result = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE FindConnectionPoint(REFIID iid, IConnectionPoint** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (node_ != Application || fixture_->profile != WithEvents || iid != PowerPointEventsIid)
            return CONNECT_E_NOCONNECTION;
        {
            std::lock_guard<std::mutex> lock(fixture_->gate);
            ++fixture_->events.pointAcquisitions;
        }
        *result = static_cast<IConnectionPoint*>(this);
        AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetConnectionInterface(IID* iid) override {
        if (!iid) return E_POINTER;
        *iid = PowerPointEventsIid;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetConnectionPointContainer(IConnectionPointContainer** result) override {
        return QueryInterface(IID_IConnectionPointContainer, reinterpret_cast<void**>(result));
    }
    HRESULT STDMETHODCALLTYPE Advise(IUnknown* unknown, DWORD* cookie) override {
        if (!unknown || !cookie) return E_POINTER;
        *cookie = 0;
        HRESULT hr = S_OK;
        {
            std::lock_guard<std::mutex> lock(fixture_->gate);
            ++fixture_->events.advises;
            if (fixture_->sink) hr = CONNECT_E_ADVISELIMIT;
            if (fixture_->eventFaultOperation == 1 &&
                ++fixture_->eventFaultSeen == fixture_->eventFaultOccurrence) hr = fixture_->eventFault;
            fixture_->events.lastAdviseHresult = hr;
        }
        if (FAILED(hr)) return hr;
        IDispatch* sink = nullptr;
        hr = unknown->QueryInterface(PowerPointEventsIid, reinterpret_cast<void**>(&sink));
        {
            std::lock_guard<std::mutex> lock(fixture_->gate);
            ++fixture_->events.sinkQueries;
            fixture_->events.lastAdviseHresult = hr;
            if (SUCCEEDED(hr)) {
                fixture_->sink = sink;
                *cookie = fixture_->cookie = ++fixture_->nextCookie;
                ++fixture_->events.sinkAddRefs;
                ++fixture_->events.activeConnections;
            }
        }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Unadvise(DWORD cookie) override {
        IDispatch* sink = nullptr;
        HRESULT hr = S_OK;
        {
            std::lock_guard<std::mutex> lock(fixture_->gate);
            ++fixture_->events.unadvises;
            if (!fixture_->sink || cookie != fixture_->cookie) hr = CONNECT_E_NOCONNECTION;
            if (fixture_->eventFaultOperation == 2 &&
                ++fixture_->eventFaultSeen == fixture_->eventFaultOccurrence) hr = fixture_->eventFault;
            fixture_->events.lastUnadviseHresult = hr;
            if (SUCCEEDED(hr)) {
                sink = fixture_->sink;
                fixture_->sink = nullptr;
                fixture_->cookie = 0;
                --fixture_->events.activeConnections;
                ++fixture_->events.sinkReleases;
            }
        }
        // 不在账本锁内回调 CCW 的 Release。
        if (sink) sink->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE EnumConnections(IEnumConnections** result) override {
        if (result) *result = nullptr;
        return E_NOTIMPL;
    }
};

void Fixture::DropOwner() {
    {
        std::lock_guard<std::mutex> lock(gate);
        if (released) return;
        released = true;
    }
    for (UINT i = 0; i < NodeCount; ++i) {
        DispatchNode* node = nodes[i];
        nodes[i] = nullptr;
        if (node) node->Release();
    }
}

std::mutex RegistryGate;
std::map<UINT, std::unique_ptr<Fixture>> Registry;
UINT NextId = 0;
Fixture* Find(UINT id) {
    std::lock_guard<std::mutex> lock(RegistryGate);
    const auto item = Registry.find(id);
    return item == Registry.end() ? nullptr : item->second.get();
}
}

#define EXPORT extern "C" __declspec(dllexport)
EXPORT UINT WINAPI FixtureMachine() {
#if defined(_M_ARM64)
    return IMAGE_FILE_MACHINE_ARM64;
#elif defined(_M_X64)
    return IMAGE_FILE_MACHINE_AMD64;
#else
    return IMAGE_FILE_MACHINE_I386;
#endif
}

EXPORT HRESULT WINAPI FixtureCreate(UINT profile, UINT* id, IDispatch** application,
    IDispatch** presentation, IDispatch** window) {
    if (!id || !application || !presentation || !window || profile > WithEvents) return E_INVALIDARG;
    *id = 0;
    *application = *presentation = *window = nullptr;
    try {
        std::lock_guard<std::mutex> registryLock(RegistryGate);
        std::unique_ptr<Fixture> fixture(new Fixture(++NextId, profile));
        HRESULT hr = S_OK;
        for (UINT node = 0; node < NodeCount; ++node) {
            fixture->nodes[node] = new (std::nothrow) DispatchNode(fixture.get(), node);
            hr = fixture->nodes[node] ? fixture->nodes[node]->Initialize() : E_OUTOFMEMORY;
            if (FAILED(hr)) { fixture->DropOwner(); return hr; }
        }
        // 先把账本/owner 放入 registry；内存失败时局部 owner 会清掉已建节点。
        Fixture* retained = fixture.get();
        Registry.emplace(retained->id, std::move(fixture));
        *id = retained->id;
        *application = retained->nodes[Application];
        *presentation = retained->nodes[Presentation];
        *window = retained->nodes[Window];
        (*application)->AddRef(); (*presentation)->AddRef(); (*window)->AddRef();
        return S_OK;
    }
    catch (...) { return E_OUTOFMEMORY; }
}

EXPORT HRESULT WINAPI FixtureSetFault(UINT id, UINT node, DISPID member,
    HRESULT failure, UINT occurrence) {
    Fixture* fixture = Find(id);
    if (!fixture || node >= NodeCount || !Supports(node, member) || SUCCEEDED(failure)) return E_INVALIDARG;
    std::lock_guard<std::mutex> lock(fixture->gate);
    fixture->faultNode = node;
    fixture->faultMember = member;
    fixture->faultHresult = failure;
    fixture->faultOccurrence = occurrence;
    fixture->faultSeen = 0;
    return S_OK;
}

EXPORT HRESULT WINAPI FixtureAcquireNode(UINT id, UINT node, IDispatch** result) {
    Fixture* fixture = Find(id);
    if (!fixture || node >= NodeCount || !result || fixture->released) return E_INVALIDARG;
    fixture->nodes[node]->AddRef();
    *result = fixture->nodes[node];
    return S_OK;
}

EXPORT HRESULT WINAPI FixtureGetLedger(UINT id, Ledger* result, UINT bytes) {
    Fixture* fixture = Find(id);
    if (!fixture || !result || bytes != sizeof(Ledger)) return E_INVALIDARG;
    std::lock_guard<std::mutex> lock(fixture->gate);
    *result = {};
    result->version = 1;
    result->nodeCount = NodeCount;
    result->ownerReleased = fixture->released ? 1 : 0;
    for (const auto& node : fixture->ledger) {
        result->liveObjects += node.created - node.destroyed;
        result->references += node.references;
    }
    return S_OK;
}

EXPORT HRESULT WINAPI FixtureGetNodeLedger(UINT id, UINT node, NodeLedger* result, UINT bytes) {
    Fixture* fixture = Find(id);
    if (!fixture || node >= NodeCount || !result || bytes != sizeof(NodeLedger)) return E_INVALIDARG;
    std::lock_guard<std::mutex> lock(fixture->gate);
    *result = fixture->ledger[node];
    return S_OK;
}

// 不经 CLR 的对照探针：即使 ITypeInfo 损坏，也证明原始 GetIDsOfNames/Invoke 仍可工作。
EXPORT HRESULT WINAPI FixtureProbe(UINT id, UINT node, DISPID member, USHORT* type, int64_t* value) {
    Fixture* fixture = Find(id);
    if (!fixture || node >= NodeCount || !type || !value || fixture->released) return E_INVALIDARG;
    const Member* selected = nullptr;
    for (const auto& candidate : Members) if (candidate.id == member) selected = &candidate;
    if (!selected) return E_INVALIDARG;
    LPOLESTR name = const_cast<LPOLESTR>(selected->name);
    DISPID resolved;
    HRESULT hr = fixture->nodes[node]->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_INVARIANT, &resolved);
    if (FAILED(hr)) return hr;
    DISPPARAMS parameters = {};
    VARIANT index;
    VariantInit(&index);
    index.vt = VT_I4;
    index.lVal = 1;
    if (selected->indexed) { parameters.cArgs = 1; parameters.rgvarg = &index; }
    VARIANT result;
    VariantInit(&result);
    hr = fixture->nodes[node]->Invoke(resolved, IID_NULL, LOCALE_INVARIANT,
        selected->kind == INVOKE_FUNC ? DISPATCH_METHOD : DISPATCH_PROPERTYGET,
        &parameters, &result, nullptr, nullptr);
    *type = result.vt;
    *value = result.vt == VT_I4 ? result.lVal : result.vt == VT_BSTR ? SysStringLen(result.bstrVal) : 0;
    VariantClear(&result);
    return hr;
}

EXPORT HRESULT WINAPI FixtureReleaseOwner(UINT id) {
    Fixture* fixture = Find(id);
    if (!fixture) return E_INVALIDARG;
    // 不能靠关闭 source 来掩盖漏掉的 Unadvise；失败时保持现场给测试报告。
    {
        std::lock_guard<std::mutex> lock(fixture->gate);
        if (fixture->events.activeConnections != 0) return E_UNEXPECTED;
    }
    fixture->DropOwner();
    return S_OK;
}

EXPORT HRESULT WINAPI FixtureGetEventLedger(UINT id, EventLedger* result, UINT bytes) {
    Fixture* fixture = Find(id);
    if (!fixture || !result || bytes != sizeof(EventLedger)) return E_INVALIDARG;
    std::lock_guard<std::mutex> lock(fixture->gate);
    *result = fixture->events;
    return S_OK;
}

EXPORT HRESULT WINAPI FixtureSetEventFault(UINT id, UINT operation, HRESULT failure, UINT occurrence) {
    Fixture* fixture = Find(id);
    if (!fixture || fixture->profile != WithEvents || (operation != 1 && operation != 2) ||
        SUCCEEDED(failure) || occurrence == 0) return E_INVALIDARG;
    std::lock_guard<std::mutex> lock(fixture->gate);
    fixture->eventFaultOperation = operation;
    fixture->eventFault = failure;
    fixture->eventFaultOccurrence = occurrence;
    fixture->eventFaultSeen = 0;
    return S_OK;
}

EXPORT HRESULT WINAPI FixtureFireEvent(UINT id, DISPID member, UINT copies, UINT includeCancel) {
    Fixture* fixture = Find(id);
    if (!fixture || fixture->profile != WithEvents || fixture->released || copies > 2) return E_INVALIDARG;
    IDispatch* sink;
    {
        std::lock_guard<std::mutex> lock(fixture->gate);
        sink = fixture->sink;
        if (!sink) return CONNECT_E_NOCONNECTION;
        ++fixture->events.sinkAddRefs;
    }
    sink->AddRef();
    VARIANT arguments[3];
    for (auto& argument : arguments) VariantInit(&argument);
    VARIANT_BOOL cancel = VARIANT_FALSE;
    UINT offset = includeCancel ? 1 : 0;
    if (includeCancel) {
        arguments[0].vt = VT_BOOL | VT_BYREF;
        arguments[0].pboolVal = &cancel;
    }
    DispatchNode* argumentNode = fixture->nodes[member == 2014 || member == 2025 ? Presentation : Window];
    for (UINT i = 0; i < copies; ++i) {
        argumentNode->AddRef();
        arguments[offset + i].vt = VT_DISPATCH;
        arguments[offset + i].pdispVal = argumentNode;
    }
    DISPPARAMS parameters = {};
    parameters.cArgs = copies + offset;
    parameters.rgvarg = arguments;
    VARIANT result;
    VariantInit(&result);
    EXCEPINFO exception = {};
    HRESULT hr = sink->Invoke(member, IID_NULL, LOCALE_INVARIANT, DISPATCH_METHOD,
        &parameters, &result, &exception, nullptr);
    VariantClear(&result);
    for (auto& argument : arguments) VariantClear(&argument);
    SysFreeString(exception.bstrSource);
    SysFreeString(exception.bstrDescription);
    SysFreeString(exception.bstrHelpFile);
    sink->Release();
    {
        std::lock_guard<std::mutex> lock(fixture->gate);
        ++fixture->events.callbacks;
        ++fixture->events.sinkReleases;
        fixture->events.emittedArguments += copies;
        fixture->events.clearedArguments += copies;
        fixture->events.lastInvokeHresult = hr;
        fixture->events.lastCancel = cancel;
    }
    return hr;
}

EXPORT HRESULT WINAPI FixtureProbeTypeInfo(UINT id, UINT node, UINT* count, HRESULT* typeInfoResult) {
    Fixture* fixture = Find(id);
    if (!fixture || node >= NodeCount || !count || !typeInfoResult || fixture->released) return E_INVALIDARG;
    HRESULT hr = fixture->nodes[node]->GetTypeInfoCount(count);
    if (FAILED(hr)) return hr;
    ITypeInfo* info = nullptr;
    *typeInfoResult = fixture->nodes[node]->GetTypeInfo(0, LOCALE_INVARIANT, &info);
    if (info) info->Release();
    return S_OK;
}

EXPORT HRESULT WINAPI FixtureForget(UINT id) {
    std::lock_guard<std::mutex> lock(RegistryGate);
    const auto item = Registry.find(id);
    if (item == Registry.end() || !item->second->released) return E_INVALIDARG;
    if (item->second->events.activeConnections != 0 ||
        item->second->events.sinkAddRefs != item->second->events.sinkReleases) return E_UNEXPECTED;
    for (const auto& node : item->second->ledger)
        if (node.references != 0 || node.created != node.destroyed) return E_UNEXPECTED;
    Registry.erase(item);
    return S_OK;
}
