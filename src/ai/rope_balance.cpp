// Rope balancing and the cleanup handlers used by the original STL.
#include "king.h"
#include <string.h>

extern "C" void* __cdecl Stl_Allocate(unsigned bytes);
extern "C" void __cdecl List_FreeNode(void* node, int bytes);
extern "C" void __fastcall Rope_Unref(BYTE* node, void*);
extern "C" void __cdecl Rope_AddLeafToForest(BYTE* node, BYTE** forest);
extern "C" void __cdecl Rope_AddSubtreeToForest(BYTE* node, BYTE** forest);
extern "C" BYTE* __cdecl Rope_Concat(BYTE* left, BYTE* right);

struct RopeNode {
    unsigned length;
    LONG references;
    BYTE kind, reserved[3];
    BYTE balanced, depth;
    void* cached;
    BYTE* left;
    BYTE* right;
};
static_assert(sizeof(RopeNode) == 28 && offsetof(RopeNode, left) == 20, "x86 rope node");

struct LengthError {
    void* vtable;
    char* baseMessage;
    int ownsBaseMessage;
    char text[256];
};
static_assert(sizeof(LengthError) == 268, "VC6 length_error object");

static void ThrowLengthError(const char* text)
{
    const unsigned size = strlen(text) + 1;
    char* temporary = static_cast<char*>(Stl_Allocate(size));
    memcpy(temporary, text, size);
    struct ReleaseString {
        char* data;
        unsigned size;
        ~ReleaseString() { List_FreeNode(data, size); }
    } release = {temporary, size};
    LengthError error = {KING_ADDR(0x64b96c), 0, 0};
    strcpy(error.text, temporary); // Both stock messages fit the inline buffer.
    _CxxThrowException(&error, reinterpret_cast<_ThrowInfo*>(KING_ADDR(0x65c548)));
}

// [FUN_462d20]
extern "C" void __fastcall Stl_ReportStringLengthError(void*)
{
    ThrowLengthError(reinterpret_cast<const char*>(KING_ADDR(0x66f544)));
}

static void DropReference(BYTE* node)
{
    if (node)
        Rope_Unref(node, 0);
}

// Stock installs a reference guard around each of the three concatenations.
static BYTE* JoinForestPieces(BYTE* left, BYTE* right, BYTE* owned)
{
    BYTE* joined;
    try {
        joined = Rope_Concat(left, right);
    } catch (...) {
        DropReference(owned);
        throw;
    }
    RopeNode* node = reinterpret_cast<RopeNode*>(joined);
    const unsigned* minimum = reinterpret_cast<const unsigned*>(KING_ADDR(0x64b984));
    if (node->length >= minimum[node->depth])
        node->balanced = 1;
    return joined;
}

// [FUN_469520] Carry a rope piece through the Fibonacci-sized forest buckets.
extern "C" void __cdecl Rope_AddLeafToForest(BYTE* leaf, BYTE** forest)
{
    const unsigned* minimum = reinterpret_cast<const unsigned*>(KING_ADDR(0x64b984));
    unsigned length = reinterpret_cast<RopeNode*>(leaf)->length;
    int i = 0;
    BYTE* accumulated = 0;
    for (; length >= minimum[i + 1]; ++i) {
        if (forest[i]) {
            BYTE* old = accumulated;
            accumulated = JoinForestPieces(forest[i], old, old);
            DropReference(forest[i]);
            forest[i] = 0;
            DropReference(old);
        }
    }
    BYTE* piece = JoinForestPieces(accumulated, leaf, accumulated);
    DropReference(accumulated);
    for (;; ++i) {
        if (forest[i]) {
            BYTE* old = piece;
            piece = JoinForestPieces(forest[i], old, old);
            DropReference(forest[i]);
            forest[i] = 0;
            DropReference(old);
        }
        if (i == 45 || reinterpret_cast<RopeNode*>(piece)->length < minimum[i + 1]) {
            forest[i] = piece;
            return;
        }
    }
}

// [FUN_462850] Merge the 46 Fibonacci buckets and check the resulting depth.
extern "C" BYTE* __cdecl Rope_Balance(BYTE* root)
{
    BYTE* forest[46] = {};
    BYTE* result = 0;
    try {
        RopeNode* node = reinterpret_cast<RopeNode*>(root);
        if (node->balanced) {
            Rope_AddLeafToForest(root, forest);
        } else {
            Rope_AddSubtreeToForest(node->left, forest);
            Rope_AddSubtreeToForest(node->right, forest);
        }
        for (BYTE*& leaf : forest) {
            if (!leaf)
                continue;
            BYTE* old = result;
            result = Rope_Concat(leaf, result);
            Rope_Unref(leaf, 0);
            leaf = 0;
            if (old)
                Rope_Unref(old, 0);
        }
    } catch (...) {
        if (result)
            Rope_Unref(result, 0);
        for (BYTE* leaf : forest)
            if (leaf)
                Rope_Unref(leaf, 0);
        throw;
    }
    if (reinterpret_cast<RopeNode*>(result)->depth > 45)
        ThrowLengthError(reinterpret_cast<const char*>(KING_ADDR(0x66f534)));
    return result;
}

// [FUN_45a5e0] The caller owns the two child references until the join succeeds.
extern "C" BYTE* __cdecl Rope_TreeConcat(BYTE* left, BYTE* right)
{
    RopeNode* l = reinterpret_cast<RopeNode*>(left);
    RopeNode* r = reinterpret_cast<RopeNode*>(right);
    RopeNode* node = static_cast<RopeNode*>(Stl_Allocate(sizeof(RopeNode)));
    if (node) {
        node->length = l->length + r->length;
        node->references = 1;
        node->kind = 1;
        node->balanced = 0;
        node->depth = static_cast<BYTE>(1 + (l->depth < r->depth ? r->depth : l->depth));
        node->cached = 0;
        node->left = left;
        node->right = right;
    }
    BYTE* joined = reinterpret_cast<BYTE*>(node);
    if (node->depth <= 20 || (node->length >= 1000 && node->depth <= 45))
        return joined;
    BYTE* result;
    try {
        result = Rope_Balance(joined);
    } catch (...) {
        List_FreeNode(node, sizeof(RopeNode));
        throw;
    }
    Rope_Unref(joined, 0);
    return result;
}

extern "C" BYTE* __cdecl Rope_JoinOwned(BYTE* left, BYTE* right)
{
    try {
        return Rope_TreeConcat(left, right);
    } catch (...) {
        // Stock releases the two acquired references on a failed join.
        DropReference(left);
        DropReference(right);
        throw;
    }
}
