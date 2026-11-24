// Layouts used by the verified 32-bit Scheme evaluator.
namespace script_forms {
using Constructor = BYTE* (__fastcall*)(BYTE*, void*);
using Destructor = void (__fastcall*)(void**, int);
using TypeId = int (__fastcall*)(BYTE*);
using TypeName = const char* (__fastcall*)(BYTE*);
using Step = void (__fastcall*)(BYTE*, void*, BYTE*);

#define FORM(name) extern "C" BYTE* __fastcall name##_Construct(BYTE*, void*); \
                   extern "C" void __fastcall name##_Destroy(void**, int);
FORM(Lambda) FORM(Begin) FORM(If) FORM(Quote) FORM(Set) FORM(Define) FORM(Eval) FORM(CallCC) FORM(Apply)
#undef FORM

struct Form {
    const char* name;
    DWORD constructor, destructor, vtable;
    Constructor construct;
    Destructor destroy;
};
static const Form forms[] = {
    {"Lambda", 0x4a81d0,0x4a8570,0x64bf80,Lambda_Construct,Lambda_Destroy},
    {"Begin",  0x4a8580,0x4a86e0,0x64bf94,Begin_Construct,Begin_Destroy},
    {"If",     0x4a86f0,0x4a8a80,0x64bfa8,If_Construct,If_Destroy},
    {"Quote",  0x4a8a90,0x4a8bc0,0x64bfbc,Quote_Construct,Quote_Destroy},
    {"Set",    0x4a8bd0,0x4a9030,0x64bfd0,Set_Construct,Set_Destroy},
    {"Define", 0x4a9040,0x4a9760,0x64bfe4,Define_Construct,Define_Destroy},
    {"Eval",   0x4a9770,0x4a9a60,0x64bff8,Eval_Construct,Eval_Destroy},
    {"CallCC", 0x4a9a70,0x4a9e70,0x64c00c,CallCC_Construct,CallCC_Destroy},
    {"Apply",  0x4a9e80,0x4aa150,0x64c020,Apply_Construct,Apply_Destroy}
};

struct Value { DWORD bits, aux, tag; };
struct Frame { void** methods; Value expression; Value* begin; Value* end; Value* capacity; int state; Value scope; };
struct Pair { DWORD header[5]; Value first, rest; };
static_assert(sizeof(Frame)==44 && sizeof(Pair)==44 && sizeof(Value)==12, "Steam 8.1 evaluator layout");
static Value Ref(Pair& pair) { return {(DWORD)&pair,0,2}; }
static Value Character(int c) { return {(DWORD)c,0,0x10008}; }
static bool Same(Value a, Value b) { return a.bits==b.bits && a.aux==b.aux && a.tag==b.tag; }

extern "C" BYTE* __fastcall EvalFrame_Construct(BYTE*,void*);
static bool Quoted(Step step, BYTE* form, bool original, Value quoted, int priorResults) {
    Frame frames[2]={};
    auto construct=original ? (Constructor)0x4a7060 : EvalFrame_Construct;
    for (auto& frame : frames) construct((BYTE*)&frame,nullptr);
    Value results[8]={};
    frames[0].begin=results; frames[0].end=results+priorResults; frames[0].capacity=results+8;
    Pair args={};
    args.first=quoted;
    args.header[1]=3;
    frames[1].expression=Ref(args);
    BYTE context[128]={};
    F(Frame*,context,108)=frames; F(Frame*,context,112)=frames+2; F(Frame*,context,116)=frames+2;
    step(form,nullptr,context);
    return F(Frame*,context,112)==frames+1 && frames[0].end==results+priorResults+1 &&
           args.header[1]==2 && Same(results[priorResults],quoted);
}

static bool Conditional(Step step, BYTE* form, Value condition, bool takeFirst) {
    // Enter the actual handler with the operator already evaluated. Its first
    // dispatch evaluates the condition. Its second selects the next expression.
    Pair args={}, thenArm={}, elseArm={};
    args.header[1]=thenArm.header[1]=elseArm.header[1]=4;
    args.first=condition; args.rest=Ref(thenArm);
    thenArm.first=Character('T'); thenArm.rest=Ref(elseArm); elseArm.first=Character('F');
    Value values[8]={{(DWORD)form,0,34}};
    Frame frame={nullptr,Ref(args),values,values+1,values+8};
    BYTE context[128]={};
    F(Frame*,context,108)=&frame; F(Frame*,context,112)=&frame+1;
    F(Frame*,context,116)=&frame+1;
    // Keep the operator alive after the frame releases its reference.
    F(int,form,4)=2;
    step(form,nullptr,context);
    if (frame.state!=1 || frame.end!=values+2 || !Same(values[1],condition) ||
        !Same(frame.expression,Ref(thenArm))) return false;
    step(form,nullptr,context);
    return frame.state==0 && frame.end==values && F(Frame*,context,112)==&frame+1 &&
           Same(frame.expression,takeFirst ? thenArm.first : elseArm.first) && F(int,form,4)==1;
}

static bool Check(bool original) {
    int errors=0, conditions=0, quotes=0;
    for (const Form& f : forms) {
        DWORD storage[]={0,0xffffffff,0xffffffff,0xabcd1234};
        BYTE* node=(BYTE*)storage;
        auto ctor=original ? (Constructor)f.constructor : f.construct;
        auto dtor=original ? (Destructor)f.destructor : f.destroy;
        DWORD base=original ? 0x64b000 : (DWORD)KingRdata;
        if (ctor(node,nullptr)!=node || storage[0]!=base+(f.vtable-0x64b000) ||
            storage[1] || storage[2] || storage[3]!=0xabcd1234) { ++errors; continue; }
        void** vtable=*(void***)node;
        if (((TypeId)vtable[0])(node)!=34 || strcmp(((TypeName)vtable[4])(node),f.name)) ++errors;
        if (!strcmp(f.name,"If")) {
            const Value inputs[]={{0,0,0x10007},{DWORD(-1),0,0x10007},Character(0),Character('A')};
            for (const Value& input : inputs) {
                if (!Conditional((Step)vtable[2],node,input,input.tag!=0x10007 || input.bits!=0)) ++errors;
                ++conditions;
            }
        }
        if (!strcmp(f.name,"Quote")) {
            Pair held={};
            const Value inputs[]={{},{0,0,0x10007},Character('Z'),Ref(held)};
            for (const Value& input : inputs) for (int prior : {0,1}) {
                held.header[1]=3;
                if (!Quoted((Step)vtable[2],node,original,input,prior) ||
                    held.header[1]!=(input.tag==2 ? 4u : 3u)) ++errors;
                ++quotes;
            }
        }
        dtor((void**)node,0);
        if (storage[0]!=base+(0x64bee4-0x64b000) || storage[3]!=0xabcd1234) ++errors;
    }
    printf("script forms original=%d forms=9 conditions=%d quotes=%d mismatches=%d\n",original,conditions,quotes,errors);
    return !errors && conditions==4 && quotes==8;
}
}
