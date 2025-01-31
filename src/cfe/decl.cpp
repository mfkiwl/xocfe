/*@
Copyright (c) 2013-2014, Su Zhenyu steven.known@gmail.com
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Su Zhenyu nor the names of its contributors
      may be used to endorse or promote products derived from this software
      without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
@*/
#include "cfeinc.h"
#include "cfecommacro.h"

//Example to show the structure of class Decl.
//  e.g1: int * a, * const * volatile b[10];
//  declaration----
//      |          |--type-spec (int)
//      |          |--declarator1 (DCL_DECLARATOR)
//      |                |---decl-type (id:a)
//      |                |---decl-type (pointer)
//      |
//      |          |--declarator2 (DCL_DECLARATOR)
//      |                |---decl-type (id:b)
//      |                |---decl-type (array:dim=10)
//      |                |---decl-type (pointer:volatile)
//      |                |---decl-type (pointer:const)
//
//  e.g2: int (*q)[30];
//  declaration----
//      |          |--type-spec (int)
//      |          |--declarator1 (DCL_DECLARATOR)
//      |                |---decl-type (id:q)
//      |                |---decl-type (pointer)
//      |                |---decl-type (array:dim=30)
//
//  e.g3: unsigned long const (* const c)(void);
//  declaration----
//                |--type-spec (unsigned long const)
//                |--declarator1 (DCL_DECLARATOR)
//                      |---decl-type (id:c)
//                      |---decl-type (pointer:const)
//                      |---decl-type (function)
//
//  e.g4: USER_DEFINED_TYPE var;
//  declaration----
//                |--type-spec (T_SPEC_USER_TYPE)
//                |--declarator (DCL_DECLARATOR)
//                      |---decl-type (id:var)
//
//  e.g5: Abstract declarator, often used in parameters.
//  int *
//  declaration----
//                |--type-spec (int)
//                |--declarator (DCL_ABS_DECLARATOR)
//                      |---nullptr
//
//  e.g6: double (*arr[10][40])[20][30];
//  Note the lowest dimension, which iterates most slowly, is at the most right
//  of decl-type list.
//  In this example, array:dim=40 is the lowest dimension.
//  declaration----
//      |         |--type-spec (double)
//      |         |--declarator1 (DCL_DECLARATOR)
//      |               |---decl-type (id:arr)
//      |               |---decl-type (array:dim=10)
//      |               |---decl-type (array:dim=40)
//      |               |---decl-type (pointer)
//      |               |---decl-type (array:dim=20)
//      |               |---decl-type (array:dim=30)
//
//The layout of Declaration:
//Decl, with DECL_dt is DCL_DECLARATION or DCL_TYPE_NAME
//    |->Scope
//    |->TypeSpec Specifier
//        |->const|volatile
//        |->void|long|int|short|char|float|double|signed|unsigned|struct|union
//        |->auto|register|static|extern|typedef
//    |->DCL_DECLARATOR | DCL_ABS_DECLARATOR
//        |->DCL_ID(optional)->DCL_FUN->DCL_POINTER->...

static Tree * initializer(TypeSpec * qua);
static Decl * declarator(TypeSpec * qua);
static TypeSpec * specifier_qualifier_list();
static Decl * struct_declaration();
static Decl * struct_declarator_list(TypeSpec * qua);
static Decl * abstract_declarator(TypeSpec * qua);
static Decl * pointer(TypeSpec ** qua);
static INT compute_array_dim(Decl * dclr, bool allow_dim0_is_empty);
static Tree * refine_tree_list(Tree * t);
static bool is_enum_const_name_exist(Enum const* e,
                                     CHAR const* ev_name,
                                     OUT INT * idx);
static bool is_enum_id_exist(EnumList const* e_list,
                             CHAR const* e_id_name,
                             OUT Enum ** e);
static INT format_base_type_spec(StrBuf & buf, TypeSpec const* ty);
static INT format_struct_union(StrBuf & buf, TypeSpec const* ty);
static UINT computeArrayByteSize(TypeSpec const* spec, Decl const* decl);

#ifdef _DEBUG_
UINT g_decl_counter = 1;
#endif
INT g_alignment = PRAGMA_ALIGN; //default alignment.
CHAR const* g_dcl_name [] = { //character of DCL enum-type.    
    "",
    "ARRAY",
    "POINTER",
    "FUN",
    "ID",
    "VARIABLE",
    "TYPE_NAME",
    "DECLARATOR",
    "DECLARATION",
    "ABS_DECLARATOR",
};


static void * xmalloc(unsigned long size)
{
    void * p = smpoolMalloc(size, g_pool_tree_used);
    ASSERT0(p != nullptr);
    ::memset(p, 0, size);
    return p;
}


//Complement the INT specifier.
//e.g: unsigned a => unsigned int a
//    register a => register int a
static void complement_qua(TypeSpec * ty)
{
    ASSERT0(ty);

    INT des = TYPE_des(ty);
    if (des == T_SPEC_UNSIGNED ||
        des == T_SPEC_SIGNED ||
        des == T_STOR_STATIC ||
        des == T_STOR_EXTERN ||
        des == T_STOR_REG ||
        des == T_STOR_AUTO) {
        SET_FLAG(TYPE_des(ty), T_SPEC_INT);
    }
}


//size: byte size.
//align: the alignment of 'size' should conform.
//       Note 0 indicates there is requirement to field align.
static UINT pad_align(UINT size, UINT align)
{
    ASSERT0(align > 0);
    if (size % align != 0) {
        size = (size / align + 1) * align;
    }
    return size; 
}


//The function computes the byte offset after appending a field that size is
//'field_size'.
//field_align: 0 indicates there is requirement to field align.
static UINT compute_field_ofst_consider_pad(Aggr const* st, UINT ofst,
                                            UINT field_size, UINT elemnum,
                                            UINT field_align)
{
    //return ofst + (UINT)xcom::ceil_align(field_size, STRUCT_align(st));
    if (field_align != 0) {
        ofst = pad_align(ofst, field_align);
    } else {
        ofst = pad_align(ofst, field_size);
    }
    ASSERTN(elemnum >= 1, ("at least one element"));
    return ofst + field_size * elemnum;
}


static UINT compute_field_ofst_consider_pad(Aggr const* st, UINT ofst,
                                            Decl const* field, UINT elemnum,
                                            UINT field_align)
{
    return compute_field_ofst_consider_pad(st, ofst, get_decl_size(field),
                                           elemnum, field_align);
}


//Calculate byte size of pure decl-type list, but without the 'specifier'.
//There only 2 type of decl-type: pointer and array.
//e.g  Given type is: int *(*p)[3][4], and calculating the
//  size of '*(*) [3][4]'.
//  The order of decl is: p->*->[3]->[4]->*
static UINT getDeclaratorSize(TypeSpec const* spec, Decl const* d)
{
    if (d == nullptr) { return 0; }
    if (is_pointer(d)) { return BYTE_PER_POINTER; }
    if (is_array(d)) { return computeArrayByteSize(spec, d); }
    return 0;
}


//Copy Decl of src is DCL_TYPE_NAME, or generate TYPE_NAME accroding
//to src information.
Decl * cp_typename(Decl const* src)
{
    if (DECL_dt(src) == DCL_TYPE_NAME) {
        return cp_decl_fully(src);
    }

    //Generate type_name.
    Decl * type_name = new_decl(DCL_TYPE_NAME);
    DECL_spec(type_name) = cp_spec(DECL_spec(src));

    Decl * decl_list_child = DECL_child(DECL_decl_list(src));
    ASSERT0(decl_list_child && DECL_dt(decl_list_child) == DCL_ID);

    Decl * decl_list = new_decl(DCL_ABS_DECLARATOR);
    DECL_child(decl_list) = cp_decl_begin_at(DECL_next(decl_list_child));

    DECL_decl_list(type_name) = decl_list;

    return type_name;
}


//Copy whole Decl, include all its specifier, qualifier, and declarator.
Decl * cp_decl_fully(Decl const* src)
{
    Decl * res = nullptr;
    ASSERT0(src);
    if (DECL_dt(src) == DCL_DECLARATION ||
        DECL_dt(src) == DCL_TYPE_NAME) {
        res = cp_decl(src);
        DECL_spec(res) = cp_spec(DECL_spec(src));
        DECL_decl_list(res) = cp_decl(DECL_decl_list(src));
        if (DECL_decl_list(res) != nullptr) {
            ASSERT0(DECL_dt(DECL_decl_list(res)) == DCL_DECLARATOR ||
                    DECL_dt(DECL_decl_list(res)) == DCL_ABS_DECLARATOR);
            DECL_child(DECL_decl_list(res)) = cp_decl_begin_at(
                DECL_child(DECL_decl_list(src)));
        }
    } else if (DECL_dt(src) == DCL_DECLARATOR ||
               DECL_dt(src) == DCL_ABS_DECLARATOR) {
        res = cp_decl(src);
        DECL_child(res) = cp_decl_begin_at(DECL_child(src));
    } else {
        ASSERT0(DECL_dt(src) == DCL_ARRAY ||
                 DECL_dt(src) == DCL_POINTER ||
                 DECL_dt(src) == DCL_FUN ||
                 DECL_dt(src) == DCL_ID ||
                 DECL_dt(src) == DCL_VARIABLE);
        res = cp_decl_begin_at(src);
    }
    return res;
}


//Only copy 'src', excepting its field.
Decl * cp_decl(Decl const* src)
{
    Decl * q = new_decl(DECL_dt(src));
    ::memcpy(q, src, sizeof(Decl));
    DECL_spec(q) = nullptr;
    DECL_decl_list(q) = nullptr;
    DECL_child(q) = nullptr;
    DECL_prev(q) = nullptr;
    DECL_next(q) = nullptr;
    return q;
}


//Duplication declarator list begin at 'header'
Decl * cp_decl_begin_at(Decl const* header)
{
    if (header == nullptr) { return nullptr; }
    Decl * newl = nullptr, * p;
    while (header != nullptr) {
        p = cp_decl(header);
        xcom::add_next(&newl, p);
        header = DECL_next(header);
    }
    return newl;
}


Decl * new_decl(DCL dcl_type)
{
    Decl * d = (Decl*)xmalloc(sizeof(Decl));
    DECL_dt(d) = dcl_type;
    #ifdef _DEBUG_
    DECL_uid(d) = g_decl_counter++;
    #endif
    return d;
}


//Construct declaration.
//'spec': specifier
//'declor': declarator.
Decl * new_declaration(TypeSpec * spec, Decl * declor, Scope * sc,
                       Tree * inittree)
{
    Decl * declaration = new_decl(DCL_DECLARATION);
    DECL_decl_scope(declaration) = sc;
    DECL_spec(declaration) = spec;
    Decl * declarator = new_decl(DCL_DECLARATOR);
    DECL_child(declarator) = declor;
    DECL_decl_list(declaration) = declarator;
    if (inittree != nullptr) {
        DECL_is_init(declarator) = true;
        DECL_init_tree(declarator) = inittree;
    }
    return declaration;
}


//Construct new declaration within given scope.
//Front-end dependent.
Decl * new_var_decl(IN Scope * scope, CHAR const* name)
{
    Decl * declaration = new_decl(DCL_DECLARATION);
    DECL_decl_scope(declaration) = scope;

    //Make TypeSpec
    TypeSpec * ty = new_type();
    TYPE_des(ty) |= T_SPEC_VOID;
    DECL_spec(declaration) = ty;

    //Make Tree node.
    Tree * tree = allocTreeNode(TR_ID, 0);
    Sym * sym = g_fe_sym_tab->add(name);
    TREE_id(tree) = sym;

    //Make DCL_DECLARATOR.
    Decl * declor = new_decl(DCL_DECLARATOR);
    Decl * id = new_decl(DCL_ID);
    DECL_id(id) = tree;
    DECL_child(declor) = id;

    //
    DECL_decl_list(declaration) = declor;
    return declaration;
}


Tree * get_decl_id_tree(Decl * dcl)
{
    while (dcl == nullptr) {
        if (DECL_dt(dcl) == DCL_ID) {
            return DECL_id(dcl);
        }
    }
    return nullptr;

}


Decl const* get_decl_id(Decl const* dcl)
{
    ASSERT0(dcl != nullptr);
    Decl const* pdcl = get_pure_declarator(dcl);
    while (pdcl != nullptr) {
        if (DECL_dt(pdcl) == DCL_ID) {
            return pdcl;
        }
        pdcl = DECL_next(pdcl);
    }
    return nullptr;
}


Decl const* get_return_type(Decl const* dcl)
{
    ASSERT0(DECL_dt(dcl) == DCL_DECLARATION);
    Decl const* retty = gen_type_name(DECL_spec(dcl));
    Decl const* tylst = get_pure_declarator(dcl);

    ASSERTN(DECL_dt(tylst) == DCL_ID,
        ("'id' should be declarator-list-head. Illegal function declaration"));
    Decl * func_type = DECL_next(tylst);
    ASSERTN(DECL_dt(func_type) == DCL_FUN, ("must be function type"));
    Decl * return_type = DECL_next(func_type);
    if (return_type == nullptr) {
        return retty;
    }

    PURE_DECL(retty) = cp_decl_begin_at(return_type);
    return retty;
}


CHAR const* get_decl_name(Decl * dcl)
{
    Sym * sym = get_decl_sym(dcl);
    if (sym == nullptr) { return nullptr; }
    return SYM_name(sym);
}


Sym * get_decl_sym(Decl const* dcl)
{
    dcl = get_decl_id(dcl);
    if (dcl != nullptr) {
        return TREE_id(DECL_id(dcl));
    }
    return nullptr;
}


//Return true if dcl declared with 'inline'.
bool is_inline(Decl const* dcl)
{
    ASSERTN(DECL_dt(dcl) == DCL_DECLARATION, ("requires declaration"));
    TypeSpec const* ty = DECL_spec(dcl);
    ASSERT0(ty);
    if (IS_INLINE(ty)) {
        return true;
    }
    return false;
}


//Return true if dcl declared with 'const'.
bool is_constant(Decl const* dcl)
{
    ASSERTN(DECL_dt(dcl) == DCL_DECLARATION, ("requires declaration"));
    TypeSpec const* ty = DECL_spec(dcl);
    ASSERT0(ty);
    if (IS_CONST(ty)) {
        return true;
    }
    return false;
}


//Return true if dcl has initial value.
bool is_initialized(Decl const* dcl)
{
    ASSERTN(dcl &&
            (DECL_dt(dcl) == DCL_DECLARATION ||
             DECL_dt(dcl) == DCL_DECLARATOR), ("requires declaration"));
    if (DECL_dt(dcl) == DCL_DECLARATION) {
        dcl = DECL_decl_list(dcl); //get DCRLARATOR
        ASSERTN(DECL_dt(dcl) == DCL_DECLARATOR ||
                DECL_dt(dcl) == DCL_ABS_DECLARATOR, ("requires declaration"));
    }
    if (DECL_is_init(dcl)) {
        return true;
    }
    return false;
}


void set_decl_init_tree(Decl const* decl, Tree * initval)
{
    ASSERT0(DECL_dt(decl) == DCL_DECLARATION);
    Decl * dclor = DECL_decl_list(decl); //get DCRLARATOR
    ASSERTN(DECL_dt(dclor) == DCL_DECLARATOR ||
            DECL_dt(dclor) == DCL_ABS_DECLARATOR, ("requires declaration"));
    if (initval == nullptr) {
        DECL_is_init(dclor) = false;
    } else {
        DECL_is_init(dclor) = true;
    }
    DECL_init_tree(dclor) = initval;
}


Tree * get_decl_init_tree(Decl const* dcl)
{
    ASSERT0(is_initialized(dcl));
    if (DECL_dt(dcl) == DCL_DECLARATION) {
        dcl = DECL_decl_list(dcl); //get DCRLARATOR
        ASSERTN(DECL_dt(dcl) == DCL_DECLARATOR ||
                DECL_dt(dcl) == DCL_ABS_DECLARATOR, ("requires declaration"));
    }
    ASSERT0(DECL_is_init(dcl));
    ASSERT0(DECL_init_tree(dcl));
    return DECL_init_tree(dcl);
}


bool is_volatile(Decl const* dcl)
{
    ASSERTN(DECL_dt(dcl) == DCL_DECLARATION, ("requires declaration"));
    TypeSpec const* ty = DECL_spec(dcl);
    ASSERT0(ty);
    if (IS_VOLATILE(ty)) {
        return true;
    }
    return false;
}


bool is_restrict(Decl const* dcl)
{
    ASSERTN(DECL_dt(dcl) == DCL_DECLARATION, ("needs declaration"));
    if (is_pointer(dcl)) {
        Decl const* x = get_pointer_decl(dcl);
        ASSERT0(x);
        TypeSpec const* ty = DECL_qua(x);
        if (ty != nullptr && IS_RESTRICT(ty)) {
            return true;
        }
    }
    return false;
}


bool is_global_variable(Decl const* dcl)
{
    ASSERTN(DECL_dt(dcl) == DCL_DECLARATION, ("needs declaration"));
    Scope const* sc = DECL_decl_scope(dcl);
    ASSERTN(sc, ("variable must be allocated within a scope."));
    if (SCOPE_level(sc) == GLOBAL_SCOPE) {
        return true;
    }
    if (SCOPE_level(sc) >= FUNCTION_SCOPE && is_static(dcl)) {
        return true;
    }
    return false;
}


bool is_static(Decl const* dcl)
{
    ASSERTN(DECL_dt(dcl) == DCL_DECLARATION, ("needs declaration"));
    ASSERTN(DECL_spec(dcl), ("miss specify type"));
    if (IS_STATIC(DECL_spec(dcl))) {
        return true;
    }
    return false;
}


bool is_local_variable(Decl const* dcl)
{
    ASSERTN(DECL_dt(dcl)==DCL_DECLARATION, ("needs declaration"));
    Scope const* sc = DECL_decl_scope(dcl);
    ASSERTN(sc, ("variable must be allocated within a scope."));
    if (SCOPE_level(sc) >= FUNCTION_SCOPE && !is_static(dcl)) {
        return true;
    }
    return false;
}


//Abstract declarator does not have ID.
bool is_abs_declaraotr(Decl const* declarator)
{
    ASSERT0(declarator);
    declarator = get_pure_declarator(declarator);
    if (declarator == nullptr) { return true; }

    Sym const* id = get_decl_sym(declarator);
    if (id == nullptr) { return true; }

    return false;
}


//Return true if dcl is daclared with user defined type.
//e.g: typedef int * INTP;  INTP xx; xx is user type referrence.
bool is_user_type_ref(Decl const* dcl)
{
    ASSERT0(DECL_dt(dcl) == DCL_DECLARATION ||
            DECL_dt(dcl) == DCL_TYPE_NAME);
    ASSERT0(DECL_spec(dcl) != nullptr);
    return IS_USER_TYPE_REF(DECL_spec(dcl));
}


//Return ture if 'dcl' is type declaration that declared with 'typedef'.
//e.g: typedef int * INTP; where INTP is an user type declaration.
bool is_user_type_decl(Decl const* dcl)
{
    ASSERT0(DECL_dt(dcl) == DCL_DECLARATION);
    return IS_TYPEDEF(DECL_spec(dcl));
}


//Return true if struct definition is complete.
bool is_struct_complete(TypeSpec const* type)
{
    type = get_pure_type_spec(const_cast<TypeSpec*>(type));
    ASSERT0(IS_STRUCT(type));
    return TYPE_struct_type(type) != nullptr &&
           STRUCT_is_complete(TYPE_struct_type(type));
}


//Return true if aggregation definition is complete.
bool is_aggr_complete(TypeSpec const* type)
{
    type = get_pure_type_spec(const_cast<TypeSpec*>(type));
    ASSERT0(IS_AGGR(type));
    return TYPE_aggr_type(type) != nullptr &&
           AGGR_is_complete(TYPE_aggr_type(type));
}


//Return true if union definition is complete.
bool is_union_complete(TypeSpec const* type)
{
    type = get_pure_type_spec(const_cast<TypeSpec*>(type));
    ASSERT0(IS_UNION(type));
    return TYPE_union_type(type) != nullptr &&
           UNION_is_complete(TYPE_union_type(type));
}


bool is_struct_type_exist_in_cur_scope(CHAR const* tag, OUT Struct ** s)
{
    Scope * sc = g_cur_scope;
    if (is_struct_type_exist(SCOPE_struct_list(sc), tag, s)) {
        return true;
    }
    return false;
}


//Is dcl a indirection declarator,
//e.g array , pointer or function pointer
static bool is_indirection(Decl const* dcl)
{
    dcl = get_pure_declarator(dcl);
    while (dcl != nullptr) {
        switch (DECL_dt(dcl)) {
        case DCL_ARRAY:
        case DCL_POINTER:
        case DCL_FUN:
            return true;
        case DCL_ID:
        case DCL_VARIABLE:
            break;
        default:
            if (DECL_dt(dcl) == DCL_DECLARATION ||
                DECL_dt(dcl) == DCL_DECLARATOR ||
                DECL_dt(dcl) == DCL_ABS_DECLARATOR ||
                DECL_dt(dcl) == DCL_TYPE_NAME) {
               ASSERTN(0, ("\nunsuitable Decl type locate here"
                           " in is_indirection()\n"));
            }
        }
        dcl = DECL_next(dcl);
    }
    return false;
}


bool is_extern(Decl const*dcl)
{
    return IS_EXTERN(DECL_spec(dcl));
}


//name: unique symbol for each of scope.
//dcl:   DCL_DECLARATION info
bool is_decl_exist_in_outer_scope(CHAR const* name, OUT Decl ** dcl)
{
    Scope const* scope = g_cur_scope;
    Decl * dr = nullptr, * dcl_list = nullptr;
    while (scope != nullptr) {
        dcl_list = SCOPE_decl_list(scope);
        while (dcl_list != nullptr) {//declaration list
            dr = dcl_list;
            dcl_list = DECL_next(dcl_list);
            Sym * sym = get_decl_sym(dr);
            if (sym == nullptr) {
                continue;
            }
            if (strcmp(SYM_name(sym), name) == 0) {
                *dcl = dr;
                return true;
            }
        }
        scope = SCOPE_parent(scope);
    }
    return false;
}


//Return true if 'd1' and 'd2' are the same identifier.
bool is_decl_equal(Decl const* d1, Decl const* d2)
{
    Scope const* s1 = DECL_decl_scope(d1);
    Scope const* s2 = DECL_decl_scope(d2);
    if (s1 == s2) {
        CHAR const* name1 = SYM_name(get_decl_sym(d1));
        CHAR const* name2 = SYM_name(get_decl_sym(d2));
        if (strcmp(name1, name2) == 0) {
            return true;
        }
    }
    return false;
}


//Return true if 'decl' is unique at a list of Decl.
bool is_unique_decl(Decl const* decl_list, Decl const* decl)
{
    Decl const* dcl = decl_list;
    while (dcl != nullptr) {
        if (is_decl_equal(dcl, decl) && dcl != decl) {
            return false;
        }
        dcl = DECL_next(dcl);
    }
    return true;
}


//Distinguish the declaration and definition of variable.
//Return true if 'decl' is a declaration, otherwise it is a definition.
bool is_declaration(Decl * decl)
{
    if (DECL_is_fun_def(decl)) {
        UNREACHABLE();
    }
    return false;
}


Decl * get_decl_in_scope(CHAR const* name, Scope const* scope)
{
    Decl * dr = nullptr, * dcl_list = nullptr;
    if (scope == nullptr) {
        return nullptr;
    }

    dcl_list = SCOPE_decl_list(scope);

    while (dcl_list != nullptr) { //declaration list
        dr = dcl_list;
        dcl_list = DECL_next(dcl_list);
        Sym const* sym = get_decl_sym(dr);

        if (sym == nullptr) { continue; }

        if (strcmp(SYM_name(sym), name) == 0) {
            return dr;
        }
    }

    return nullptr;
}


//Reference an user defined type-name.
static TypeSpec * typedef_name(TypeSpec * ty)
{
    Decl * ut = nullptr;
    if (g_real_token != T_ID) return nullptr;
    if (!is_user_type_exist_in_outer_scope(g_real_token_string, &ut)) {
        return nullptr;
    }
    if (ty == nullptr) {
        ty = new_type();
    }
    TYPE_des(ty) |= T_SPEC_USER_TYPE;
    TYPE_user_type(ty)= ut;
    match(T_ID);
    return ty;
}


static INT ck_type_spec_legally(TypeSpec * ty)
{
    INT des = TYPE_des(ty);
    StrBuf buf1(64);
    StrBuf buf2(64);
    //struct or union
    BYTE c1 = (HAVE_FLAG(des, T_SPEC_STRUCT) ||
               HAVE_FLAG(des, T_SPEC_UNION)) != 0,
         c2 = HAVE_FLAG(des, T_SPEC_ENUM) != 0,
         c3 = is_simple_base_type(ty) != 0,
         c4 = HAVE_FLAG(des, T_SPEC_USER_TYPE) != 0;

    //signed
    if (ONLY_HAVE_FLAG(des, T_SPEC_SHORT)) {
        //des only contained SHORT
        return ST_SUCC;
    }
    if (ONLY_HAVE_FLAG(des, T_SPEC_SHORT|T_SPEC_INT)) {
        //des only contained SHORT and INT concurrent
        return ST_SUCC;
    }
    if (ONLY_HAVE_FLAG(des, T_SPEC_SIGNED|T_SPEC_SHORT)) { return ST_SUCC; }
    if (ONLY_HAVE_FLAG(des, T_SPEC_SIGNED|T_SPEC_SHORT|T_SPEC_INT)) {
        return ST_SUCC;
    }
    if (ONLY_HAVE_FLAG(des, T_SPEC_INT)) { return ST_SUCC; }
    if (ONLY_HAVE_FLAG(des, T_SPEC_SIGNED|T_SPEC_INT)) { return ST_SUCC; }
    if (ONLY_HAVE_FLAG(des, T_SPEC_SIGNED|T_SPEC_LONGLONG)) { return ST_SUCC; }
    if (ONLY_HAVE_FLAG(des, T_SPEC_SIGNED)) { return ST_SUCC; }
    if (ONLY_HAVE_FLAG(des, T_SPEC_LONG)) { return ST_SUCC; }
    if (ONLY_HAVE_FLAG(des, T_SPEC_LONG|T_SPEC_INT)) { return ST_SUCC; }
    if (ONLY_HAVE_FLAG(des, T_SPEC_SIGNED|T_SPEC_LONG)) { return ST_SUCC; }
    if (ONLY_HAVE_FLAG(des, T_SPEC_SIGNED|T_SPEC_LONG|T_SPEC_INT)) {
        return ST_SUCC;
    }
    if (ONLY_HAVE_FLAG(des, T_SPEC_LONGLONG)) { return ST_SUCC; }
    if (ONLY_HAVE_FLAG(des, T_SPEC_LONGLONG|T_SPEC_INT)) { return ST_SUCC; }
    if (ONLY_HAVE_FLAG(des, T_SPEC_SIGNED|T_SPEC_LONGLONG)) { return ST_SUCC; }
    if (ONLY_HAVE_FLAG(des, T_SPEC_SIGNED|T_SPEC_LONGLONG|T_SPEC_INT)) {
        //des contained SIGNED, LONG, LONG, INT concurrent
        return ST_SUCC;
    }

    //unsiged
    if (ONLY_HAVE_FLAG(des, T_SPEC_UNSIGNED|T_SPEC_SHORT)) { return ST_SUCC; }
    if (ONLY_HAVE_FLAG(des, T_SPEC_UNSIGNED|T_SPEC_SHORT|T_SPEC_INT)) {
        return ST_SUCC;
    }
    if (ONLY_HAVE_FLAG(des, T_SPEC_UNSIGNED)) { return ST_SUCC; }
    if (ONLY_HAVE_FLAG(des, T_SPEC_UNSIGNED|T_SPEC_INT)) { return ST_SUCC; }
    if (ONLY_HAVE_FLAG(des, T_SPEC_UNSIGNED|T_SPEC_LONGLONG)) {
        return ST_SUCC;
    }
    if (ONLY_HAVE_FLAG(des, T_SPEC_UNSIGNED|T_SPEC_LONG)) { return ST_SUCC; }
    if (ONLY_HAVE_FLAG(des, T_SPEC_UNSIGNED|T_SPEC_LONG|T_SPEC_INT)) {
        return ST_SUCC;
    }

    if (c1 == 1 && c2 == 1) {
        err(g_real_line_num,
            "struct or union cannot compatilable with enum-type");
        return ST_ERR;
    }
    if (c1 == 1 && c3 == 1) {
        format_base_type_spec(buf1, ty);
        err(g_real_line_num,
            "struct or union cannot compatilable with '%s'", buf1.buf);
        return ST_ERR;
    }
    if (c1 == 1 && c4 == 1) {
        format_user_type_spec(buf1, ty);
        err(g_real_line_num,
            "struct or union cannot compatilable with '%s'", buf1.buf);
        return ST_ERR;
    }
    if (c2 == 1 && c3 == 1) {
        format_base_type_spec(buf1, ty);
        err(g_real_line_num, "enum-type cannot compatilable with '%s'",
            buf1.buf);
        return ST_ERR;
    }
    if (c2 == 1 && c4 == 1) {
        format_user_type_spec(buf1, ty);
        err(g_real_line_num, "enum-type cannot compatilable with '%s'",
            buf1.buf);
        return ST_ERR;
    }
    if (c3 == 1 && c4 == 1) {
        format_user_type_spec(buf1, ty);
        format_base_type_spec(buf2, ty);
        err(g_real_line_num,
            "'%s' type cannot compatilable with '%s'", buf1.buf, buf2.buf);
        return ST_ERR;
    }
    return ST_SUCC;    
}


//Extract the qualifier from 'ty' and fulfill 'qua'.
static void extract_qualifier(TypeSpec * ty, TypeSpec * qua)
{
    ASSERT0(ty && qua);
    if (IS_CONST(ty)) {
        SET_FLAG(TYPE_des(qua), T_QUA_CONST);
        REMOVE_FLAG(TYPE_des(ty), T_QUA_CONST);
    }
    if (IS_VOLATILE(ty)) {
        SET_FLAG(TYPE_des(qua), T_QUA_VOLATILE);
        REMOVE_FLAG(TYPE_des(ty), T_QUA_VOLATILE);
    }
    if (IS_RESTRICT(ty)) {
        SET_FLAG(TYPE_des(qua), T_QUA_RESTRICT);
        REMOVE_FLAG(TYPE_des(ty), T_QUA_RESTRICT);
    }
}


//Up to date, there is no other differences between
//union definition and struct definition.
static Decl * union_declaration()
{
    return struct_declaration();
}


static void consume_tok_to_semi()
{
    while (g_real_token != T_SEMI &&
           g_real_token != T_END &&
           g_real_token != T_NUL) {
        match(g_real_token);
    }
    if (g_real_token == T_SEMI) {
        match(g_real_token);
    }
}


//struct_declaration:
//    specifier-qualifier-list struct-declarator-list;
static Decl * struct_declaration()
{
    TypeSpec * type_spec = specifier_qualifier_list();
    if (type_spec == nullptr) {
        err(g_real_line_num,
            "miss qualifier, illegal member declaration of struct");
        consume_tok_to_semi();
        return nullptr;
    }

    TypeSpec * qualifier = new_type();
    extract_qualifier(type_spec, qualifier);

    Decl * dcl_list = struct_declarator_list(qualifier);
    while (dcl_list != nullptr) {
        Decl * dcl = dcl_list;
        dcl_list = DECL_next(dcl_list);
        DECL_next(dcl) = DECL_prev(dcl) = nullptr;

        Decl * declaration = new_decl(DCL_DECLARATION);
        DECL_spec(declaration) = type_spec;
        DECL_decl_list(declaration) = dcl;
        DECL_align(declaration) = g_alignment;
        DECL_decl_scope(declaration) = g_cur_scope;
        DECL_lineno(declaration) = g_real_line_num;

        if (is_user_type_decl(declaration)) {
            err(g_real_line_num,
                "illegal storage class, should not use typedef in "
                "struct/union declaration.");
            continue;
        }

        if (IS_USER_TYPE_REF(type_spec)) {
            declaration = factor_user_type(declaration);
            DECL_align(declaration) = g_alignment;
            DECL_decl_scope(declaration) = g_cur_scope;
            DECL_lineno(declaration) = g_real_line_num;
        }

        xcom::add_next(&SCOPE_decl_list(g_cur_scope), declaration);
        DECL_decl_scope(declaration) = g_cur_scope;
    }

    if (g_real_token != T_SEMI) {
        err(g_real_line_num, "expected ';' after struct declaration");
    } else {
        match(T_SEMI);
    }
    return SCOPE_decl_list(g_cur_scope);
}


static Decl * union_declaration_list()
{
    while (g_real_token != T_RLPAREN) {
        if (g_real_token == T_END ||
            g_real_token == T_NUL ||
            is_too_many_err()) {
            return SCOPE_decl_list(g_cur_scope);
        }
        union_declaration();
    }
    return SCOPE_decl_list(g_cur_scope);
}


static Decl * struct_declaration_list()
{
    while (g_real_token != T_RLPAREN) {
        if (g_real_token == T_END ||
            g_real_token == T_NUL ||
            is_too_many_err()) {
            return SCOPE_decl_list(g_cur_scope);
        }

        Decl * field_decl = nullptr;
        if ((field_decl = struct_declaration()) == nullptr) {
            break;
        }
    }
    return SCOPE_decl_list(g_cur_scope);
}


static void type_spec_struct_field(Struct * s, TypeSpec * ty)
{
    ASSERT0(s);
    match(T_LLPAREN);
    push_scope(false);
    //UINT errn = g_err_msg_list.get_elem_count();
    STRUCT_decl_list(s) = struct_declaration_list();
    if (STRUCT_decl_list(s) == nullptr) {
        //Empty field list, for compiler convenient, insert one byte field.
        Decl * var = new_var_decl(g_cur_scope, "#placeholder");
        STRUCT_decl_list(s) = var;
    }
    pop_scope();
    //if (g_err_msg_list.get_elem_count() == errn) {
    //    STRUCT_is_complete(s) = true;
    //}

    //Numbering field id.    
    UINT i = 0;
    for (Decl * field = STRUCT_decl_list(s);
         field != nullptr; field = DECL_next(field)) {
        DECL_fieldno(field) = i++;
        DECL_is_sub_field(field) = true;
        DECL_base_type_spec(field) = ty;
    }

    if (match(T_RLPAREN) != ST_SUCC) {
        err(g_real_line_num, "expected '}' after struct definition");
        return;
    }
    STRUCT_is_complete(s) = true;
}


static TypeSpec * type_spec_struct(TypeSpec * ty)
{
    TYPE_des(ty) |= T_SPEC_STRUCT;
    match(T_STRUCT);
    if (ck_type_spec_legally(ty) != ST_SUCC) {
        err(g_real_line_num, "type specifier is illegal");
        return ty;
    }

    INT alignment = g_alignment; //record alignment before struct declaration.
    Struct * s = nullptr;
    if (g_real_token == T_ID) {
        //struct definition
        //format is: 'struct' 'TAG' '{' ... '}' 'ID';
        //Find current and all of outer scope to find the
        //identical declaration or declaring.
        //C permit forward declaration, namely use first, define second.
        //e.g:struct EX * ex;
        //    struct EX { ... };
        //Here, we make an incomplete struct/union, then find the complete
        //version and refill the declaration if there are requirments to
        //access its field.
        if (!is_struct_exist_in_outer_scope(g_cur_scope,
                                            g_real_token_string, &s)) {
            s = (Struct*)xmalloc(sizeof(Struct));
            AGGR_tag(s) = g_fe_sym_tab->add(g_real_token_string);
            AGGR_is_complete(s) = false;
            AGGR_scope(s) = g_cur_scope;
            SCOPE_struct_list(g_cur_scope).append_tail(s);            
            //Note we do not append anonymous aggregate into scope list because
            //user can not find the aggregate through tag name. Thus there will
            //multiple aggregates that have same data structure layout.
        }
        match(T_ID);
    }

    if (g_real_token == T_LLPAREN) {
        if (s == nullptr) {
            //Struct format as either struct TAG {} or struct {}.
            //The struct declarated without TAG.
            s = (Struct*)xmalloc(sizeof(Struct));
            AGGR_tag(s) = nullptr;
            AGGR_is_complete(s) = false;
            AGGR_scope(s) = g_cur_scope;
        }
        if (AGGR_is_complete(s)) {
            //Report error if there exist a previous declaration.
            ASSERT0(AGGR_tag(s));
            err(g_real_line_num, "struct '%s' redefined",
                SYM_name(AGGR_tag(s)));
            return ty;
        }
        type_spec_struct_field(s, ty);
    }
    
    if (s == nullptr) {
        //There is neither 'TAG' nor '{'.
        err(g_real_line_num, "illegal use '%s'", g_real_token_string);
        return ty;
    }

    //We must update alignment always, because user may apply #pragma directive
    //anywhere.
    //e.g:#pragma align (4)
    //    struct A a1;
    //    ...
    //    #pragma align (8)
    //    struct A a2;
    //    ...
    //  In actually, a1 and a2 are implemented in different alignment.
    AGGR_align(s) = alignment;

    TYPE_struct_type(ty) = s;
    return ty;
}


static void type_spec_union_field(Union * s, TypeSpec * ty)
{
    ASSERT0(s);
    match(T_LLPAREN);
    push_scope(false);

    //UINT errn = g_err_msg_list.get_elem_count();
    AGGR_decl_list(s) = union_declaration_list();
    if (AGGR_decl_list(s) == nullptr) {
        //Empty field list, for compiler convenient, insert one byte field.
        Decl * var = new_var_decl(g_cur_scope, "#placeholder");
        AGGR_decl_list(s) = var;
    }
    pop_scope();
    //if (g_err_msg_list.get_elem_count() == errn) {
    //    UNION_is_complete(s) = true;
    //}

    //Numbering field id.    
    UINT i = 0;
    for (Decl * field = AGGR_decl_list(s);
         field != nullptr; field = DECL_next(field)) {
        DECL_fieldno(field) = i++;
        DECL_is_sub_field(field) = true;
        DECL_base_type_spec(field) = ty;
    }

    if (match(T_RLPAREN) != ST_SUCC) {
        err(g_real_line_num, "expected '}' after union definition");
        return;
    }
    AGGR_is_complete(s) = true;
}


static TypeSpec * type_spec_union(TypeSpec * ty)
{
    TYPE_des(ty) |= T_SPEC_UNION;
    match(T_UNION);
    if (ck_type_spec_legally(ty) != ST_SUCC) {
        err(g_real_line_num, "type specifier is illegal");
        return ty;
    }

    INT alignment = g_alignment; //record alignment before union declaration.
    Union * s = nullptr;
    if (g_real_token == T_ID) {
        //union definition
        //format is: 'union' 'TAG' '{' ... '}' 'ID';
        //Find current and all of outer scope to find the
        //identical declaration or declaring, and refill the
        //declaration which has been declared before.
        //C permit forward declaration, namely use first, define second.
        //e.g:union EX * ex;
        //    union EX { ... };
        //Here, we make an incomplete struct/union, then find the complete
        //versionand refill the declaration if there are requirments to
        //access its field.
        if (!is_union_exist_in_outer_scope(g_cur_scope,
                                           g_real_token_string, &s)) {
            s = (Union*)xmalloc(sizeof(Union));
            AGGR_tag(s) = g_fe_sym_tab->add(g_real_token_string);
            AGGR_is_complete(s) = false;
            AGGR_scope(s) = g_cur_scope;
            SCOPE_union_list(g_cur_scope).append_tail(s);
        }
        match(T_ID);
    }

    if (g_real_token == T_LLPAREN) {
        //'s' is a incomplete union declaration,
        //and it is permitted while we define a
        //non-pointer variable as member of 's'.
        if (s == nullptr) {
            //Union format as either union TAG {} or union {}.
            //The union declarated without TAG.
            s = (Union*)xmalloc(sizeof(Union));
            AGGR_tag(s) = nullptr;
            AGGR_is_complete(s) = false;
            AGGR_scope(s) = g_cur_scope;
            //Note we do not append anonymous aggregate into scope list because
            //user can not find the aggregate through tag name. Thus there will
            //multiple aggregates that have same data structure layout.
        }
        if (AGGR_is_complete(s)) {
            //Report error if there exist a previous declaration.
            ASSERT0(AGGR_tag(s));
            err(g_real_line_num, "union '%s' redefined", SYM_name(AGGR_tag(s)));
            return ty;
        }
        type_spec_union_field(s, ty);        
    }

    if (s == nullptr) {
        //There is neither 'TAG' nor '{'.
        err(g_real_line_num, "illegal use '%s'", g_real_token_string);
        return ty;
    }

    //We must change alignment always, because user
    //may apply #pragma align anywhere.
    //e.g
    //    #pragma align (4)
    //    union A{...} a1;
    //    ...
    //    #pragma align (8)
    //    union A a2;
    //    ...
    //So, a1 and a2 are implement as different alignment!
    AGGR_align(s) = alignment;

    TYPE_aggr_type(ty) = s;
    return ty;
}


static TypeSpec * type_spec(TypeSpec * ty)
{
    if (ty == nullptr) {
        ty = new_type();
    }
    switch (g_real_token) {
    case T_VOID:
        match(T_VOID);
        TYPE_des(ty) |= T_SPEC_VOID;
        break;
    case T_CHAR:
        match(T_CHAR);
        TYPE_des(ty) |= T_SPEC_CHAR;
        break;
    case T_SHORT:
        match(T_SHORT);
        TYPE_des(ty) |= T_SPEC_SHORT;
        break;
    case T_INT:
        match(T_INT);
        TYPE_des(ty) |= T_SPEC_INT;
        break;
    case T_LONG:
        match(T_LONG);
        if (IS_TYPE(ty, T_SPEC_LONG)) {
            //It seems longlong might confuse user.
            //warn("'long long' is not ANSI C89, "
            //      "using longlong as an alternative");
            TYPE_des(ty) &= ~T_SPEC_LONG;
            TYPE_des(ty) |= T_SPEC_LONGLONG;
        } else if (IS_TYPE(ty, T_SPEC_LONGLONG)) {
            err(g_real_line_num, "type specifier is illegal");
            return ty;
        } else {
            TYPE_des(ty) |= T_SPEC_LONG;
        }
        break;
    case T_LONGLONG:
        match(T_LONGLONG);
        TYPE_des(ty) |= T_SPEC_LONGLONG;
        break;
    case T_BOOL:
        match(T_BOOL);
        TYPE_des(ty) |= T_SPEC_BOOL;
        break;
    case T_FLOAT:
        match(T_FLOAT);
        TYPE_des(ty) |= T_SPEC_FLOAT;
        break;
    case T_DOUBLE:
        match(T_DOUBLE);
        TYPE_des(ty) |= T_SPEC_DOUBLE;
        break;
    case T_SIGNED:
        match(T_SIGNED);
        TYPE_des(ty) |= T_SPEC_SIGNED;
        break;
    case T_UNSIGNED:
        match(T_UNSIGNED);
        TYPE_des(ty) |= T_SPEC_UNSIGNED;
        break;
    case T_STRUCT:
        return type_spec_struct(ty);
    case T_UNION:
        return type_spec_union(ty);
    default:; //do nothing
    }
    return ty;
}


//enumerator:
//  identifier
//  identifier = constant_expression
static EnumValueList * enumrator()
{
    EnumValueList * evl = nullptr;
    Enum * e = nullptr;
    LONGLONG idx = 0;
    if (g_real_token != T_ID) { return evl; }

    evl = (EnumValueList*)xmalloc(sizeof(EnumValueList));
    EVAL_LIST_name(evl) = g_fe_sym_tab->add(g_real_token_string);

    if (is_enum_exist(SCOPE_enum_list(g_cur_scope),
                      g_real_token_string, &e, (INT*)&idx)) {
        err(g_real_line_num, "'%s' : redefinition , different basic type",
            g_real_token_string);
        return evl;
    }

    match(T_ID);
    if (g_real_token != T_ASSIGN) { return evl; }

    match(T_ASSIGN);
    //constant expression
    if (is_in_first_set_of_exp_list(g_real_token)) {
        Tree * t = conditional_exp();
        idx = 0;
        if (t == nullptr) {
            err(g_real_line_num, "empty constant expression");
            return evl;
        }
        if (!computeConstExp(t, &idx, 0)) {
            err(g_real_line_num, "expected constant expression");
            return evl;
        }
        EVAL_LIST_val(evl) = (INT)idx;
        return evl;
    }

    err(g_real_line_num,
        "syntax error : constant expression cannot used '%s'",
        g_real_token_string);
    return evl;
}


//enumerator_list:
//  enumerator
//  enumerator_list , enumerator
static EnumValueList * enumerator_list()
{
    EnumValueList * evl = enumrator(), * nevl = nullptr;
    if (evl == nullptr) { return nullptr; }

    EnumValueList * last = get_last(evl);
    while (g_real_token == T_COMMA) {
        match(T_COMMA);
        nevl = enumrator();
        if (nevl == nullptr) { break; }
        xcom::add_next(&evl, &last, nevl);
        last = nevl;
    }
    return evl;
}


//enum_specifier:
//  enum identifier { enumerator_list }
//  enum            { enumerator_list }
//  enum identifier
static TypeSpec * enum_spec(TypeSpec * ty)
{
    if (ty == nullptr) { ty = new_type(); }

    TYPE_des(ty) |= T_SPEC_ENUM;
    match(T_ENUM);

    if (g_real_token == T_ID) {
        //Parse enum name. Note that the name is optional.
        Sym * sym = g_fe_sym_tab->add(g_real_token_string);
        TYPE_enum_type(ty) = new_enum();
        ENUM_name(TYPE_enum_type(ty)) = sym;
        match(T_ID);
    }

    if (g_real_token == T_LLPAREN) {
        //Parse the definition of a list of enum constant.
        if (TYPE_enum_type(ty) == nullptr) { TYPE_enum_type(ty) = new_enum(); }

        match(T_LLPAREN);

        ENUM_vallist(TYPE_enum_type(ty)) = enumerator_list();

        if (match(T_RLPAREN) != ST_SUCC) {
            err(g_real_line_num, "miss '}' during enum type declaring");
            return ty;
        }

        //Check enum name if it is given. The name is optional.
        Enum * e = nullptr;
        Sym * enumname = ENUM_name(TYPE_enum_type(ty));
        if (enumname != nullptr &&
            is_enum_id_exist_in_outer_scope(SYM_name(enumname), &e)) {
            err(g_real_line_num, "'%s' : enum type redefinition",
                SYM_name(enumname));
            return ty;
        }
    }
    return ty;
}


//type_qualifier:  one of
//  const
//  volatile
static TypeSpec * quan_spec(IN TypeSpec * ty)
{
    if (ty == nullptr) {
        ty = new_type();
    }
    switch (g_real_token) {
    case T_CONST:
        match(T_CONST);
        if (IS_CONST(ty)) {
            err(g_real_line_num, "same type qualifier used more than once");
            return ty;
        }
        #if (ALLOW_CONST_VOLATILE == 1)
        SET_FLAG(TYPE_des(ty), T_QUA_CONST);
        #else
        if (IS_VOLATILE(ty)) {
            err(g_real_line_num, "variable can not both const and volatile");
            return ty;
        }
        REMOVE_FLAG(TYPE_des(ty), T_QUA_VOLATILE);
        SET_FLAG(TYPE_des(ty), T_QUA_CONST);
        #endif
        break;
    case T_VOLATILE:
        match(T_VOLATILE);
        if (IS_VOLATILE(ty)) {
            err(g_real_line_num, "same type qualifier used more than once");
            return ty;
        }

        //If there exist 'const' spec, so 'volatile' is omitted.
        #if (ALLOW_CONST_VOLATILE == 1)
        SET_FLAG(TYPE_des(ty), T_QUA_VOLATILE);
        #else
        if (IS_CONST(ty)) {
            err(g_real_line_num, "variable can not both const and volatile");
            return ty;
        }
        #endif
        break;
    case T_RESTRICT:
        match(T_RESTRICT);
        SET_FLAG(TYPE_des(ty), T_QUA_RESTRICT);
        break;
    default:;
    }
    return ty;
}


//storage_class_specifier:  one of
//  auto
//  register
//  static
//  extern
//  inline
//  typedef
static TypeSpec * stor_spec(IN TypeSpec * ty)
{
    if (ty == nullptr) { ty = new_type(); }

    if ((HAVE_FLAG(TYPE_des(ty), T_STOR_AUTO) &&
         g_real_token != T_AUTO) ||
        (!ONLY_HAVE_FLAG(TYPE_des(ty), T_STOR_AUTO) &&
         g_real_token == T_AUTO)) {
        err(g_real_line_num,
            "auto can not specified with other type-specifier");
        return nullptr;
    }

    if ((HAVE_FLAG(TYPE_des(ty), T_STOR_STATIC) &&
         g_real_token == T_EXTERN) ||
        (HAVE_FLAG(TYPE_des(ty), T_STOR_EXTERN) &&
         g_real_token == T_STATIC)) {
        err(g_real_line_num,
            "static and extern can not be specified meanwhile");
        return nullptr;
    }

    switch (g_real_token) {
    case T_AUTO:
        match(T_AUTO);
        SET_FLAG(TYPE_des(ty), T_STOR_AUTO);
        break;
    case T_REGISTER:
        match(T_REGISTER);
        SET_FLAG(TYPE_des(ty), T_STOR_REG);
        break;
    case T_STATIC:
        match(T_STATIC);
        SET_FLAG(TYPE_des(ty), T_STOR_STATIC);
        break;
    case T_EXTERN:
        match(T_EXTERN);
        SET_FLAG(TYPE_des(ty), T_STOR_EXTERN);
        break;
    case T_INLINE:
        match(T_INLINE);
        SET_FLAG(TYPE_des(ty), T_STOR_INLINE);
        break;
    case T_TYPEDEF:
        match(T_TYPEDEF);
        SET_FLAG(TYPE_des(ty), T_STOR_TYPEDEF);
        break;
    default:;
    }
    return ty;
}


static TypeSpec * SpecifierOrId(TypeSpec * ty, bool * is_return_ty)
{
    Decl * ut = nullptr;
    Struct * s = nullptr;
    Union * u = nullptr;
    if (is_user_type_exist_in_outer_scope(g_real_token_string, &ut)) {
        if (ty != nullptr) {
            if (IS_USER_TYPE_REF(ty)) {
                err(g_real_line_num, "redeclared user defined type.");
                *is_return_ty = true;
                return ty;
            }

            if (IS_STRUCT(ty)) {
                err(g_real_line_num, "redeclared struct type.");
                *is_return_ty = true;
                return ty;
            }

            if (IS_UNION(ty)) {
                err(g_real_line_num, "redeclared union type.");
                *is_return_ty = true;
                return ty;
            }
        }

        TypeSpec * p = typedef_name(ty);
        if (p == nullptr) {
            *is_return_ty = true;
            return ty;
        }
        ty = p;
        return ty;
    }

    if (is_struct_exist_in_outer_scope(g_cur_scope, g_real_token_string, &s)) {
        if (ty != nullptr) {
            if (IS_USER_TYPE_REF(ty)) {
                err(g_real_line_num, "redeclared user defined type.");
                *is_return_ty = true;
                return ty;
            }

            if (IS_STRUCT(ty)) {
                err(g_real_line_num, "redeclared struct type.");
                *is_return_ty = true;
                return ty;
            }

            if (IS_UNION(ty)) {
                err(g_real_line_num, "redeclared union type.");
                *is_return_ty = true;
                return ty;
            }
        }

        ASSERT0(s);

        if (ty == nullptr) {
            ty = new_type();
        }
        TYPE_des(ty) |= T_SPEC_STRUCT;
        TYPE_struct_type(ty) = s;
        match(T_ID);
        return ty;
    }

    if (is_union_exist_in_outer_scope(g_cur_scope, g_real_token_string, &u)) {
        if (ty != nullptr) {
            if (IS_USER_TYPE_REF(ty)) {
                err(g_real_line_num, "redeclared user defined type.");
                *is_return_ty = true;
                return ty;
            }

            if (IS_STRUCT(ty)) {
                err(g_real_line_num, "redeclared struct type.");
                *is_return_ty = true;
                return ty;
            }

            if (IS_UNION(ty)) {
                err(g_real_line_num, "redeclared union type.");
                *is_return_ty = true;
                return ty;
            }
        }

        ASSERT0(u);
        if (ty == nullptr) {
            ty = new_type();
        }
        TYPE_des(ty) |= T_SPEC_UNION;
        TYPE_union_type(ty) = u;
        match(T_ID);
        return ty;
    }

    //g_real_token is not a specifier.
    *is_return_ty = true;
    return ty;
}


//declaration_specifiers:
//    storage_class_specifier declaration_specifiers
//    storage_class_specifier
//    type_specifier declaration_specifiers
//    type_specifier
//    type_qualifier declaration_specifiers
//    type_qualifier
static TypeSpec * declaration_spec()
{
    TypeSpec * ty = nullptr;
    for (;;) {
        switch (g_real_token) {
        case T_AUTO:
        case T_REGISTER:
        case T_STATIC:
        case T_EXTERN:
        case T_INLINE:
        case T_TYPEDEF:
            ty = stor_spec(ty);
            break;
        case T_VOID:
        case T_CHAR:
        case T_SHORT:
        case T_INT:
        case T_LONGLONG:
        case T_BOOL:
        case T_LONG:
        case T_FLOAT:
        case T_DOUBLE:
        case T_SIGNED:
        case T_UNSIGNED:
        case T_STRUCT:
        case T_UNION:
            ty = type_spec(ty);
            break;
        case T_ENUM:
            ty = enum_spec(ty);
            break;
        case T_CONST:
        case T_VOLATILE:
        case T_RESTRICT:
            ty = quan_spec(ty);
            break;
        case T_ID: {
            bool is_return_ty = false;
            ty = SpecifierOrId(ty, &is_return_ty);
            if (is_return_ty) { return ty; }
            break;
        }
        default: goto END;
        }
    }
END:
    return ty;
}


//'fun_dclor': parameter list.
Decl * get_parameter_list(Decl * dcl, OUT Decl ** fun_dclor)
{
    dcl = const_cast<Decl*>(get_pure_declarator(dcl));
    while (dcl != nullptr && DECL_dt(dcl) != DCL_FUN) {
        dcl = DECL_next(dcl);
    }

    if (fun_dclor != nullptr) {
        *fun_dclor = dcl;
    }

    return DECL_fun_para_list(dcl);
}


//parameter_declaration:
//    declaration_specifiers declarator
//    declaration_specifiers abstract_declarator
//    declaration_specifiers
static Decl * parameter_declaration()
{
    Decl * declaration = new_decl(DCL_DECLARATION);
    TypeSpec * type_spec = declaration_spec();
    if (type_spec == nullptr) {
        return nullptr;
    }

    complement_qua(type_spec);

    TypeSpec * qualifier = new_type();

    //Extract qualifier, and regarded it as the qualifier
    //to the subsequently POINTER or ID.
    extract_qualifier(type_spec, qualifier);

    //'DCL_ID' should be the list-head if it exist.
    Decl * dcl_list = reverse_list(abstract_declarator(qualifier));

    DECL_spec(declaration) = type_spec;

    if (dcl_list == nullptr ||
        (dcl_list != nullptr && DECL_dt(dcl_list) == DCL_ID)) {
        DECL_decl_list(declaration) = new_decl(DCL_DECLARATOR);
    } else {
        DECL_decl_list(declaration) = new_decl(DCL_ABS_DECLARATOR);
    }

    DECL_child(DECL_decl_list(declaration)) = dcl_list;

    //array parameter has at least one elem.
    compute_array_dim(declaration, false);

    if (IS_USER_TYPE_REF(type_spec)) {
        //Factor the user defined type which via typedef.
        declaration = factor_user_type(declaration);
        DECL_align(declaration) = g_alignment;
        DECL_decl_scope(declaration) = g_cur_scope;
        DECL_lineno(declaration) = g_real_line_num;
    }

    return declaration;
}


//parameter_type_list:
//    parameter_list
//    parameter_list , ...
//parameter_list:
//    parameter_declaration
//    parameter_list , parameter_declaration
//The above bnf can covert to
//
//parameter_type_list:
//    parameter_declaration
//    parameter_declaration , parameter_declaration
//    parameter_declaration , ...
static Decl * parameter_type_list()
{
    Decl * declaration = nullptr , * t = nullptr;
    for (;;) {
        t = parameter_declaration();
        if (t == nullptr) {
            return declaration;
        }
        xcom::add_next(&declaration, t);
        if (g_real_token == T_COMMA) {
            match(T_COMMA);
        } else if (g_real_token == T_RPAREN ||
                   g_real_token == T_END ||
                   g_real_token == T_NUL ||
                   is_too_many_err()) {
            break;
        }

        //'...' must be the last parameter-declarator
        if (g_real_token == T_DOTDOTDOT) {
            match(T_DOTDOTDOT);
            t = new_decl(DCL_VARIABLE);
            xcom::add_next(&declaration, t);
            break;
        }
    }
    return declaration;
}


//direct_abstract_declarator:
//    ( abstract_declarator )
//    direct_abstract_declarator [ constant_expression ]
//                               [ constant_expression ]
//    direct_abstract_declarator [                     ]
//                               [                     ]
//                               (                     )
//                               ( parameter_type_list )
//    direct_abstract_declarator (                     )
//    direct_abstract_declarator ( parameter_type_list )
static Decl * direct_abstract_declarator(TypeSpec * qua)
{
    Decl * dcl = nullptr, * ndcl = nullptr;
    switch (g_real_token) {
    case T_LPAREN: //'(' abstract_declarator ')'
        match(T_LPAREN);
        dcl = abstract_declarator(qua);
        //Here 'dcl' can be NUL L
        if (match(T_RPAREN) != ST_SUCC) {
            err(g_real_line_num, "miss ')'");
            return dcl;
        }
        DECL_is_paren(dcl) = 1;
        break;
    case T_ID: { //identifier
        Sym * sym = g_fe_sym_tab->add(g_real_token_string);
        add_to_symtab_list(&SCOPE_sym_tab_list(g_cur_scope), sym);
        dcl = new_decl(DCL_ID);
        DECL_id(dcl) = id();
        DECL_qua(dcl) = qua;
        match(T_ID);
        break;
    }
    default:;
    }

    switch (g_real_token) {
    case T_LSPAREN: { //outer level operator is ARRAY
        Tree * t = nullptr;
        while (g_real_token == T_LSPAREN) {
            match(T_LSPAREN);
            Decl * ndcl2 = new_decl(DCL_ARRAY);
            t = conditional_exp();
            if (match(T_RSPAREN) != ST_SUCC) {
                err(g_real_line_num, "miss ']'");
                return dcl;
            }
            DECL_array_dim_exp(ndcl2) = t;

            //'id' should be the last one in declarator-list.
            xcom::insertbefore_one(&dcl, dcl, ndcl2);
        }
        break;
    }
    case T_LPAREN: {
        //current level operator is function-pointer/function-definition
        //Parameter list.
        match(T_LPAREN);
        ndcl = new_decl(DCL_FUN);
        //DECL_fun_base(ndcl) = dcl;
        push_scope(true);

        //Check if param declaration is void, such as: foo(void).
        Decl * param_decl = parameter_type_list();

        if (xcom::cnt_list(param_decl) == 1 &&
            is_any(param_decl) &&
            is_scalar(param_decl)) {
            ;
        } else {
            DECL_fun_para_list(ndcl) = param_decl;
        }

        pop_scope();
        xcom::insertbefore_one(&dcl, dcl, ndcl);
        if (match(T_RPAREN) != ST_SUCC) {
            err(g_real_line_num, "miss ')'");
            return dcl;
        }
        break;
    }
    default:;
    }
    return dcl;
}


//abstract_declarator:
//    pointer
//    pointer direct_abstract_declarator
//            direct_abstract_declarator
static Decl * abstract_declarator(TypeSpec * qua)
{
    Decl * ptr = pointer(&qua);
    Decl * dcl = direct_abstract_declarator(qua);
    if (ptr == nullptr && dcl == nullptr) {
        return nullptr;
    }
    if (dcl == nullptr) {
        return ptr;
    }
    //Keep DCL_ID is the last one if it exist.
    //e.g:
    //    ptr is '*', dcl is '[]'->'ID'
    //    return: '*'->'[]'->'ID'
    xcom::insertbefore(&dcl, dcl, ptr);    
    return dcl;
}


//specifier_qualifier_list:
//    type_specifier specifier_qualifier_list
//    type_specifier
//    type_qualifier specifier_qualifier_list
//    type_qualifier
static TypeSpec * specifier_qualifier_list()
{
    TypeSpec * ty = nullptr;
    TypeSpec * p = nullptr;
    for (;;) {
        switch (g_real_token) {
        case T_VOID:
        case T_CHAR:
        case T_SHORT:
        case T_INT:
        case T_LONGLONG:
        case T_BOOL:
        case T_LONG:
        case T_FLOAT:
        case T_DOUBLE:
        case T_SIGNED:
        case T_UNSIGNED:
        case T_STRUCT:
        case T_UNION:
            ty = type_spec(ty);
            break;
        case T_ENUM:
            ty = enum_spec(ty);
            break;
        case T_CONST:
        case T_VOLATILE:
            ty = quan_spec(ty);
            break;
        case T_ID:
            p = typedef_name(ty);
            if (p == nullptr) { return ty; }
            ty = p;
            break;
        default: goto END;
        }
    }
END:
    return ty;
}


//type_name:
//    specifier_qualifier_list abstract_declarator
//    specifier_qualifier_list
//NOTICE: Do not include user defined type
Decl * type_name()
{
    //Parse specifier and qualifier.
    //e.g: char * const*, here 'char' is specifier, '* const*' is qualifier.
    TypeSpec * type_spec = specifier_qualifier_list();
    if (type_spec == nullptr) {
        return nullptr;
    }

    //Parse POINTER/ARRAY/ID, and complement their qualifier.
    //Collect const/volatile prefix, add them to POINTER/ARRAY/ID.
    //e.g: const int a; Add const qualifier to ID 'a'.
    //    const int * a; Add const qualifier to POINTER '*'.
    TypeSpec * qualifier = new_type();
    extract_qualifier(type_spec, qualifier);
    Decl * abs_decl = abstract_declarator(qualifier);

    //Generate type_name.
    Decl * type_name = new_decl(DCL_TYPE_NAME);
    DECL_spec(type_name) = type_spec;
    DECL_decl_list(type_name) = new_decl(DCL_ABS_DECLARATOR);
    DECL_child(DECL_decl_list(type_name)) = reverse_list(abs_decl);
    complement_qua(type_spec);
    compute_array_dim(type_name, false);
    return type_name;
}


//initializer_list:
//    initializer
//    initializer_list , initializer
static Tree * initializer_list(TypeSpec * qua)
{
    Tree * t = initializer(qua);
    if (t == nullptr) { return nullptr; }

    Tree * last = get_last(t);
    while (g_real_token == T_COMMA) {
        match(T_COMMA);
        if (g_real_token == T_RLPAREN) { break; }

        Tree * nt = initializer(qua);
        if (nt == nullptr) { break; }

        xcom::add_next(&t, &last, nt);
        last = get_last(nt);
    }
    return t;
}


//initializer:
//    assignment_expression
//    { initializer_list }
//    { initializer_list , }
static Tree * initializer(TypeSpec * qua)
{
    Tree * t = nullptr, * es = nullptr;
    switch (g_real_token) {
    case T_LLPAREN: {
        UINT lineno = g_real_line_num;
        match(T_LLPAREN); 
        t = initializer_list(qua);
        if (g_real_token == T_COMMA) {
            match(T_COMMA);
            if (match(T_RLPAREN) != ST_SUCC) {
                err(g_real_line_num, "syntax error '%s'", g_real_token_string);
                return t;
            }
        } else if (match(T_RLPAREN) != ST_SUCC) {
            err(g_real_line_num, "syntax error : '%s'", g_real_token_string);
            return t;
        }
        es = allocTreeNode(TR_INITVAL_SCOPE, lineno);
        TREE_initval_scope(es) = t;
        t = es;
        return t;
    }
    default:
        if (is_in_first_set_of_exp_list(g_real_token)) {
            return exp();
        }
        if (g_real_token == T_RLPAREN) {
            //An empty {}.
            return nullptr;
        }
        err(g_real_line_num,
            "syntax error : initializing cannot used '%s'",
            g_real_token_string);
    }
    return t;
}


//struct_declarator:
//    declarator
//               : constant_expression
//    declarator : constant_expression
static Decl * struct_declarator(TypeSpec * qua)
{
    LONGLONG idx = 0;
    Decl * dclr = declarator(qua);
    if (dclr == nullptr) {
        return nullptr;
    }

    dclr = reverse_list(dclr);
    Decl * declarator = new_decl(DCL_DECLARATOR);
    DECL_child(declarator) = dclr;
    compute_array_dim(declarator, true);

    if (g_real_token == T_COLON) {
        //Bit field
        Tree * t = nullptr;
        if (is_indirection(dclr)) {
            Sym * s = get_decl_sym(dclr);
            ASSERTN(s != nullptr, ("member name cannot be nullptr"));
            err(g_real_line_num,
                "'%s' : pointer type cannot assign bit length", SYM_name(s));
            return declarator;
        }
        match(T_COLON);
        t = conditional_exp();
        if (!computeConstExp(t, &idx, 0)) {
            err(g_real_line_num, "expected constant expression");
            return declarator;
        }

        //bit length must be check in typeck.cpp
        DECL_bit_len(declarator) = (INT)idx;
        DECL_is_bit_field(declarator) = true;
    }
    return declarator;
}


//struct_declarator_list:
//    struct_declarator
//    struct_declarator_list , struct_declarator
static Decl * struct_declarator_list(TypeSpec * qua)
{
    Decl * dclr = struct_declarator(qua), * ndclr = nullptr;
    if (dclr == nullptr) { return nullptr; }

    while (g_real_token == T_COMMA) {
        match(T_COMMA);
        ndclr = struct_declarator(qua);
        xcom::add_next(&dclr, ndclr);
    }

    return dclr;
}


//Pick out the declarator.
//e.g:int * a [10];
//    the declarator is :
//      declaration
//          |->declarator
//                 |->a->[10]->*
Decl const* get_declarator(Decl const* decl)
{
    ASSERT0(decl != nullptr);
    switch (DECL_dt(decl)) {
    case DCL_TYPE_NAME:
        decl = DECL_decl_list(decl);
        ASSERTN(decl == nullptr ||
                DECL_dt(decl) == DCL_ABS_DECLARATOR,
                ("must be DCL_ABS_DECLARATOR in TYPE_NAME"));
        return decl;
    case DCL_DECLARATOR:
    case DCL_ABS_DECLARATOR:
        return decl;
    case DCL_DECLARATION:
        decl = DECL_decl_list(decl);
        ASSERT0(decl == nullptr ||
                DECL_dt(decl) == DCL_DECLARATOR ||
                DECL_dt(decl) == DCL_ABS_DECLARATOR);
        return decl;
    default: ASSERTN(0, ("Not a declarator"));
    }
    UNREACHABLE();
    return nullptr;
}


//Pick out the pure declarator specification list
//    e.g:
//        int * a [10];
//        the pure declarator list is :  a->[10]->*
//
//        int (*) [10];
//        the pure declarator list is :  *->[10]
Decl const* get_pure_declarator(Decl const* decl)
{
    ASSERT0(decl != nullptr);
    switch (DECL_dt(decl)) {
    case DCL_ARRAY:
    case DCL_POINTER:
    case DCL_ID:
    case DCL_FUN: //function-pointer type.
        break;
    case DCL_VARIABLE:
        ASSERTN(0, ("can not be in declaration"));
        break;
    case DCL_TYPE_NAME:
        decl = DECL_decl_list(decl);
        if (decl == nullptr) {
            return nullptr;
        }
        ASSERTN(DECL_dt(decl) == DCL_ABS_DECLARATOR,
                ("must be DCL_ABS_DECLARATOR in TYPE_NAME"));
        decl = DECL_child(decl);
        break;
    case DCL_DECLARATOR:
    case DCL_ABS_DECLARATOR:
        decl = DECL_child(decl);
        break;
    case DCL_DECLARATION:
        decl = DECL_decl_list(decl);
        if (decl == nullptr) {
            return nullptr;
        }
        ASSERT0(DECL_dt(decl) == DCL_DECLARATOR ||
                DECL_dt(decl) == DCL_ABS_DECLARATOR);
        decl = DECL_child(decl);
        break;
    default: ASSERTN(0, ("unknown Decl"));
    }
    return decl;
}


//Return the dimension of given array.
//Note array should be DCL_DECLARATION or DCL_TYPE_NAME.
UINT get_array_dim(Decl const* arr)
{
    ASSERT0(DECL_dt(arr) == DCL_DECLARATION || DECL_dt(arr) == DCL_TYPE_NAME);
    ASSERT0(is_array(arr));
    Decl * dclr = const_cast<Decl*>(get_pure_declarator(arr));
    //Get the first ARRAY decl-type.
    while (dclr != nullptr) {
        if (DECL_dt(dclr) == DCL_ARRAY) { break; }
        dclr = DECL_next(dclr);
    }

    //Count the dimension.
    UINT ndim = 0;
    while (dclr != nullptr) {
        if (DECL_dt(dclr) != DCL_ARRAY) { break; }
        dclr = DECL_next(dclr);
        ndim++;
    }
    return ndim;
}


//Get the number of element to given dimension.
//Note the field DECL_array_dim of array is only
//available after compute_array_dim() invoked, and
//it compute the really number of array element via
//DECL_array_dim_exp, that is a constant expression.
//'arr': array declaration.
//'dim': given dimension to compute, start at 0 which is the closest dimension
//    to leading ID, in decl-type list.
//    e.g: int arr[8][12][24];
//    In C language, [24] is the lowest dimension. 
//    where decl-type list will be: 
//      ID:'arr' -> ARRAY[8] -> ARRAY[12] -> ARRAY[24]
//    dim 0 indicates ARRAY[8], the highest dimension of 'arr'.
ULONG get_array_elemnum_to_dim(Decl const* arr, UINT dim)
{
    Decl const* dcl = get_first_array_decl(const_cast<Decl*>(arr));
    ASSERT0(dcl);
    UINT i = 0;
    while (i < dim && dcl != nullptr) {
        if (DECL_dt(dcl) != DCL_ARRAY) {
            break;
        }
        dcl = DECL_next(dcl);
        i++;
    }

    if (dcl == nullptr || DECL_dt(dcl) != DCL_ARRAY) {
        return 0;
    }
    return (ULONG)DECL_array_dim(dcl);
}


//Get the number of elements in entire array.
ULONG get_array_elemnum(Decl const* arr)
{
    UINT dn = get_array_dim(arr);
    UINT en = 1;
    for (UINT i = 0; i < dn; i++) {
        en *= get_array_elemnum_to_dim(arr, i);
    }
    return en;
}


//Get the bytesize of array element.
ULONG get_array_elem_bytesize(Decl const* arr)
{
    ASSERT0(is_array(arr));
    ASSERT0(DECL_spec(arr));
    return getSpecTypeSize(DECL_spec(arr));
}


//Calculate constant expression when parsing the variable
//declaration of array type.
//
//'allow_dim0_is_empty': parameter array's lowest dimension size
//can NOT be zero.
static INT compute_array_dim(Decl * dclr, bool allow_dim0_is_empty)
{
    //e.g: int (*(a[1][2]))[3][4];
    BYTE dim = 0;
    INT st = ST_SUCC;
    dclr = const_cast<Decl*>(get_pure_declarator(dclr));
    while (dclr != nullptr) {
        if (DECL_dt(dclr) == DCL_ARRAY) {
            dim++;
        } else {
            //Recompute dim size for next array type:
            //e.g: int (*(a[1][2]))[3][4];
            //pure dclr: ID(a)->[1]->[2]->PTR->[3]->[4]
            dim = 0;
        }

        if (dim >= 1) {
            Tree * t = DECL_array_dim_exp(dclr);
            LONGLONG idx = 0;
            if (t == nullptr) {
                if (dim > 1) {
                    err(g_real_line_num,
                        "size of dimension %dth can not be zero,"
                        " may be miss subscript",
                        dim);
                    st = ST_ERR;
                    goto NEXT;
                }
                if (!allow_dim0_is_empty) {
                    //If size of dim0 is 0, set it to 1 by default means
                    //the array contain at least one element.
                    idx = 1;
                }
            } else if (t != nullptr) {
                if (!computeConstExp(t, &idx, 0)) {
                    err(g_real_line_num, "expected constant expression");
                    st = ST_ERR;
                    goto NEXT;
                }
                 if (idx < 0 || idx > MAX_ARRAY_INDX) {
                    err(g_real_line_num,
                        "negative subscript or subscript is too large");
                    st = ST_ERR;
                    goto NEXT;
                }
                if (idx == 0 && t != nullptr) {
                    err(g_real_line_num,
                        "cannot allocate an array of constant size 0");
                    st = ST_ERR;
                    goto NEXT;
                }
            }
            DECL_array_dim(dclr) = idx;
        }
NEXT:
        dclr = DECL_next(dclr);
    }
    return st;
}


//init_declarator:
//    declarator
//    declarator = initializer
static Decl * init_declarator(TypeSpec * qua)
{
    Decl * dclr = declarator(qua);
    if (dclr == nullptr) { return nullptr; }
    dclr = reverse_list(dclr);

    //dclr is DCL_DECLARATOR node
    Decl * declarator = new_decl(DCL_DECLARATOR);
    DECL_child(declarator) = dclr;
    compute_array_dim(declarator,
        true ); //array dim size should be determined by init value.

    if (g_real_token == T_ASSIGN) {
        match(T_ASSIGN);
        DECL_init_tree(declarator) = initializer(qua);
        if (DECL_init_tree(declarator) == nullptr ||
            (TREE_type(DECL_init_tree(declarator)) == TR_INITVAL_SCOPE &&
             TREE_initval_scope(DECL_init_tree(declarator)) == nullptr)) {
            warn(g_real_line_num, "initial value is empty");

            //TO BE CONFIRMED: Do we allow an empty initialization?
            //err(g_real_line_num, "initial value is nullptr");
            //Give up parsing subsequent tokens if initialization is empty.
            //suck_tok_to(0, T_SEMI, T_END, T_NUL);
        }
        DECL_is_init(declarator) = 1;
    }
    return declarator;
}


//init_declarator_list:
//    init_declarator
//    init_declarator_list, init_declarator
static Decl * init_declarator_list(TypeSpec * qua)
{
    Decl * dclr = init_declarator(qua);
    if (dclr == nullptr) {
        return nullptr;
    }
    while (g_real_token == T_COMMA) {
        match(T_COMMA);
        Decl * ndclr = init_declarator(qua);
        xcom::add_next(&dclr, ndclr);
    }
    return dclr;
}


//ARRAY MODE:     S (D)[e]
//POINTER MODE:   S * D
//FUNCTION MODE:  S (D)(p)
//
//direct_declarator:
//    identifier
//    (declarator)
//    direct_declarator [ constant_expression ]
//    direct_declarator [                     ]
//    direct_declarator ( parameter_type_list )
//    //direct_declarator ( identifier_list ) obsolete C proto
//    direct_declarator (                 )
static Decl * direct_declarator(TypeSpec * qua)
{
    INT is_paren = 0;
    Decl * dcl = nullptr;
    switch (g_real_token) {
    case T_LPAREN: //'(' declarator ')'
        match(T_LPAREN);
        dcl = declarator(qua);
        if (match(T_RPAREN) != ST_SUCC) {
            err(g_real_line_num, "miss ')'");
            goto FAILED;
        }
        if (dcl == nullptr) {
            err(g_real_line_num, "must have identifier declared");
            goto FAILED;
        }
        is_paren = 1;
        break;
    case T_ID: { //identifier
        Sym * sym = g_fe_sym_tab->add(g_real_token_string);
        add_to_symtab_list(&SCOPE_sym_tab_list(g_cur_scope), sym);
        dcl = new_decl(DCL_ID);
        DECL_id(dcl) = id();
        DECL_qua(dcl) = qua;
        match(T_ID);
        break;
    }
    default:;
    }

    if (dcl == nullptr) { return nullptr; }

    switch (g_real_token) {
    case T_LSPAREN: { //'[', the declarator is an array.
        Tree * t = nullptr;
        while (g_real_token == T_LSPAREN) {
            match(T_LSPAREN);
            Decl * ndcl = new_decl(DCL_ARRAY);
            t = conditional_exp();
            if (match(T_RSPAREN) != ST_SUCC) {
                err(g_real_line_num, "miss ']'");
                goto FAILED;
            }
            DECL_array_dim_exp(ndcl) = t;
            DECL_is_paren(ndcl) = is_paren;

            //'id' should be the last one in declarator-list.
            xcom::insertbefore_one(&dcl, dcl, ndcl);
        }
        break;
    }
    case T_LPAREN: { //'(', the declarator is a function decl/def.
        match(T_LPAREN);
        Decl * ndcl = new_decl(DCL_FUN);
        push_scope(true);

        //Check if param declaration is void, such as: foo(void).
        Decl * param_decl = parameter_type_list();
        if (xcom::cnt_list(param_decl) == 1 &&
            is_any(param_decl) &&
            is_scalar(param_decl)) {
            ;
        } else {
            DECL_fun_para_list(ndcl) = param_decl;
        }

        pop_scope();
        DECL_is_paren(ndcl) = is_paren;
        xcom::insertbefore_one(&dcl, dcl, ndcl);
        if (match(T_RPAREN) != ST_SUCC) {
            err(g_real_line_num, "miss ')'");
            goto FAILED;
        }
        break;
    }
    default:; //do nothing
    }
    return dcl;
FAILED:
    return dcl;
}


//Copy specifier.
TypeSpec * cp_spec(TypeSpec * ty)
{
    TypeSpec * new_ty = new_type();
    new_ty->copy(*ty);
    return new_ty;
}


//pointer:
//    '*' type-qualifier-list(pass)
//    '*' type-qualifier-list(pass) pointer
static Decl * pointer(TypeSpec ** qua)
{
    Decl * ndcl = nullptr;
    TypeSpec * new_qua = *qua;
    while (g_real_token == T_ASTERISK) {
        match(T_ASTERISK);
        Decl * dcl = new_decl(DCL_POINTER);
        DECL_qua(dcl) = new_qua;
        new_qua = new_type();
        quan_spec(new_qua);
        if (IS_RESTRICT(new_qua)) {
            SET_FLAG(TYPE_des(DECL_qua(dcl)), T_QUA_RESTRICT);
            REMOVE_FLAG(TYPE_des(new_qua), T_QUA_RESTRICT);
        }
        xcom::add_next(&ndcl, dcl);
    }
    quan_spec(new_qua); //match qualifier for followed ID.
    *qua = new_qua;
    return ndcl;
}


//declarator:
//    pointer  direct_declarator
//             direct_declarator
static Decl * declarator(TypeSpec * qua)
{
    Decl * ptr = pointer(&qua);
    Decl * dclr = direct_declarator(qua);
    if (dclr == nullptr) {
        return nullptr;
    }

    //e.g:
    //    ptr is '*', dclr is '[]'->'ID'
    //    return: '*'->'[]'->'ID'
    xcom::insertbefore(&dclr, dclr, ptr);
    return dclr; //'id' is the list tail.
}


static INT label_ck(Scope * s)
{
    if (s == nullptr) { return ST_ERR; }
    LabelInfo * lref = SCOPE_ref_label_list(s).get_head();
    LabelInfo * lj = nullptr;
    while (lref != nullptr) {
        CHAR * name = SYM_name(LABELINFO_name(lref));
        ASSERT0(name);
        LabelInfo * li = SCOPE_label_list(s).get_head();
        for (; li != nullptr; li = SCOPE_label_list(s).get_next()) {
            if (strcmp(SYM_name(LABELINFO_name(li)), name) == 0) {
                set_lab_used(li);
                break;
            }
        }
        if (li == nullptr) {
            err(map_lab2lineno(lref), "label '%s' was undefined", name);
            return ST_ERR;
        }
        lref = SCOPE_ref_label_list(s).get_next();
    }

    lj = SCOPE_label_list(s).get_head();
    for (; lj != nullptr; lj = SCOPE_label_list(s).get_next()) {
        if (!is_lab_used(lj)) {
            warn(0, "'%s' unreferenced label", SYM_name(LABELINFO_name(lj)));
        }
    }
    return ST_SUCC;
}


void dump_decl(Decl const* dcl, StrBuf & buf)
{
    if (g_logmgr == nullptr) { return; }
    format_declaration(buf, dcl);
    note(g_logmgr, "\n%s\n", buf.buf);
}


void dump_decl(Decl const* dcl)
{
    format_declaration(dcl, g_logmgr->getIndent());
}


static void fix_para_array_index(Decl * decl)
{
    ASSERT0(DECL_dt(decl) == DCL_DECLARATION);
    TypeSpec * ty = nullptr;
    ASSERT0(DECL_is_formal_para(decl));
    ASSERT0(is_pointer(decl));
    Decl * d = get_pointer_base_decl(decl, &ty);
    if (d == nullptr || DECL_dt(d) == DCL_POINTER) { return; }

    if (DECL_dt(d) == DCL_ARRAY && DECL_array_dim(d) == 0) {
        //Fix array index, it can not be 0.
        //C allows the first dimension of parameter be zero.
        //e.g: void foo (char p[][20]) is legal syntax, but
        //    the declaration is char p[1][20].
        DECL_array_dim(d) = 1;
    }

    if (getDeclaratorSize(DECL_spec(decl), d) == 0) {
        err(g_real_line_num,
            "Only the first dimension size can be 0, "
            "the lower dimension size can not be 0");
    }
}


//Change array to pointer if it is formal parameter.
//Fulfill the first dimension to at least 1 if it is a parameter.
static Tree * refineArray(Tree * t)
{
    ASSERT0(TREE_type(t) == TR_ARRAY);

    //Formal parameter of array type is a pointer in actually.
    //Insert a Dereference to comfort the C specification.
    Tree * base = TREE_array_base(t);
    if (TREE_type(base) != TR_ID) { return t; }

    //ID is unique to its scope.
    CHAR * name = SYM_name(TREE_id(base));
    ASSERT0(TREE_id_decl(base));
    Scope * s = DECL_decl_scope(TREE_id_decl(base));
    Decl * decl = get_decl_in_scope(name, s);
    ASSERT0(decl != nullptr);
    if (!DECL_is_formal_para(decl)) { return t; }

    //Verfiy and fix formal parameters with array type.
    //Check if decl is pointer that pointed to an array.
    //e.g: 'int (*p)[]'
    //the referrence should do same operation as its declaration.
    Decl const* base_of_pt = get_pure_declarator(decl);
    if (DECL_dt(base_of_pt) == DCL_ID) {
        base_of_pt = DECL_next(base_of_pt);
    }

    if (base_of_pt != nullptr && DECL_dt(base_of_pt) == DCL_POINTER) {
        if (DECL_next(base_of_pt) != nullptr &&
            DECL_dt(DECL_next(base_of_pt)) == DCL_ARRAY) {
            base_of_pt = DECL_next(base_of_pt);
        }
    }

    if (base_of_pt != nullptr && DECL_dt(base_of_pt) == DCL_ARRAY) {
        //The base of pointer is an array. Convert a[] to (*a)[].
        Tree * deref = allocTreeNode(TR_DEREF, TREE_lineno(base));
        TREE_lchild(deref) = base;
        setParent(deref, TREE_lchild(deref));
        TREE_array_base(t) = deref;
        setParent(t, TREE_array_base(t));
        fix_para_array_index(decl);
    }        
    return t;
}


//Do refinement and amendment for tree.
//    * Revise formal parameter. In C spec, formal array is pointer
//      that point to an array in actually.
static Tree * refine_tree(Tree * t)
{
    if (t == nullptr) { return nullptr; }
    if (TREE_type(t) == TR_ARRAY) {
        t = refineArray(t);
    } else if (TREE_type(t) == TR_SCOPE) {
        Scope * s = TREE_scope(t);
        SCOPE_stmt_list(s) = refine_tree_list(SCOPE_stmt_list(s));
    }

    for (UINT i = 0; i < MAX_TREE_FLDS; i++) {
        refine_tree_list(TREE_fld(t, i));
    }
    return t;
}


static Tree * refine_tree_list(Tree * t)
{
    if (t == nullptr) { return nullptr; }
    Tree * head = t;
    int i = 0;
    while (t != nullptr) {
        refine_tree(t);
        t = TREE_nsib(t);
        i++;
    }
    return head;
}


//Convert Tree in terms of C specification.
static void refine_func(Decl * func)
{
    Scope * scope = DECL_fun_body(func);
    Tree * t = SCOPE_stmt_list(scope);
    if (t != nullptr) {
        t = refine_tree_list(t);
        if (g_err_msg_list.get_elem_count() == 0) {
            ASSERTN(TREE_parent(t) == nullptr,
                    ("parent node of Tree is nullptr"));
        }
        SCOPE_stmt_list(scope) = t;
    }
}


//Converts 'decl' into pointer type from its origin type.
//NOTCIE: 'decl' cannot be pointer.
//
//'decl': a declarator, not a pointer type.
//'is_append': transform to pointer type by appending a DCL_POINTER.
//    In order to achieve, insert DCL_POINTER type before
//    the first array type.
//    e.g: ID->ARRAY->ARRAY => ID->POINTER->ARRAY->ARRAY
Decl * trans_to_pointer(Decl * decl, bool is_append)
{
    ASSERTN(DECL_dt(decl) == DCL_DECLARATION, ("only DCRLARATION is valid"));
    ASSERTN(!is_pointer(decl), ("only DCRLARATION is valid"));
    Decl * pure = const_cast<Decl*>(get_pure_declarator(decl));
    Decl * p = pure;
    Decl * new_pure = nullptr;
    bool isdo = true;
    INT count = 0;
    while (pure != nullptr) {
        switch (DECL_dt(pure)) {
        case DCL_FUN: //Function declarator
        case DCL_ID: //Identifier
        case DCL_VARIABLE: //Variable length parameter
        case DCL_POINTER: //POINTER  declarator
            {
                if (count > 0) {
                    isdo = false;
                }
                p = cp_decl(pure);
                xcom::add_next(&new_pure, p);
                break;
            }
        case DCL_ARRAY: //ARRAY declarator
            {
                if (is_append) {
                    is_append = false;
                    p = new_decl(DCL_POINTER);
                    xcom::add_next(&new_pure, p);
                    isdo = false;
                }

                if (!isdo) {
                    p = cp_decl(pure);
                    DECL_is_paren(p) = 1;
                    xcom::add_next(&new_pure, p);
                } else {
                    count++;
                    p = new_decl(DCL_POINTER);
                    xcom::add_next(&new_pure, p);
                }
                break;
            }
        case DCL_TYPE_NAME:
            //if current decl is TYPE_NAME,  it descript a declarator

        case DCL_DECLARATOR:  //declarator
        case DCL_DECLARATION: //declaration
        case DCL_ABS_DECLARATOR: //abstract declarator
        default:
            ASSERTN(0, ("unexpected Decl type over here"));
        }
        pure = DECL_next(pure);
    }

    PURE_DECL(decl) = new_pure;
    ASSERTN(is_pointer(decl), ("transform failed!"));
    return decl;
}


//Return Enum if it has exist in 'elst', otherwise return nullptr.
Enum * find_enum(EnumList * elst , Enum * e)
{
    if (elst == nullptr || e == nullptr) { return nullptr; }
    EnumList * p = elst;
    while (p != nullptr) {
        if (ENUM_LIST_enum(p) == e) {
            return e;
        }
        p = ENUM_LIST_next(p);
    }
    return nullptr;
}


//Return nullptr indicate we haven't found it in 'ut_list', and
//append 'ut' to tail of the list as correct, otherwise return
//the finded one.
Decl * addToUserTypeList(UserTypeList ** ut_list , Decl * decl)
{
   if (ut_list == nullptr || decl == nullptr) return nullptr;
   if ((*ut_list) == nullptr) {
        *ut_list = (UserTypeList*)xmalloc(sizeof(UserTypeList));
        USER_TYPE_LIST_utype(*ut_list) = decl;
        return nullptr;
   } else {
        UserTypeList * p = *ut_list, * q = nullptr;
        while (p != nullptr) {
            q = p;
            if (USER_TYPE_LIST_utype(p) == decl) {
                //'sym' already exist, return 'sym' as result
                return decl;
            }
            p = USER_TYPE_LIST_next(p);
        }
        USER_TYPE_LIST_next(q) = (UserTypeList*)xmalloc(sizeof(UserTypeList));
        USER_TYPE_LIST_prev(USER_TYPE_LIST_next(q)) = q;
        q = USER_TYPE_LIST_next(q);
        USER_TYPE_LIST_utype(q) = decl;
    }
    return nullptr;
}


//Return true if enum-value existed in current scope.
bool is_enum_exist(EnumList const* e_list,
                   CHAR const* e_name,
                   OUT Enum ** e,
                   OUT INT * idx)
{
    if (e_list == nullptr || e_name == nullptr) return false;
    EnumList const* el = e_list;
    while (el != nullptr) {
        if (is_enum_const_name_exist(ENUM_LIST_enum(el), e_name, idx)) {
            *e = ENUM_LIST_enum(el);
            return true;
        }
        el = ENUM_LIST_next(el);
    }
    return false;
}


//Enum typed identifier is effective at all of outer scopes.
bool is_enum_id_exist_in_outer_scope(CHAR const* cl, OUT Enum ** e)
{
    Scope * sc = g_cur_scope;
    while (sc != nullptr) {
        if (is_enum_id_exist(SCOPE_enum_list(sc), cl, e)) {
            return true;
        }
        sc = SCOPE_parent(sc);
    }
    return false;
}


bool is_aggr_exist_in_outer_scope(Scope * scope,
                                  CHAR const* tag,
                                  TypeSpec const* spec,
                                  OUT Aggr ** s)
{
    if (is_struct(spec)) {
        return is_struct_exist_in_outer_scope(scope, tag, (Struct**)s);
    }
    ASSERT0(is_union(spec));
    return is_union_exist_in_outer_scope(scope, tag, (Union**)s);
}


bool is_aggr_exist_in_outer_scope(Scope * scope,
                                  Sym const* tag,
                                  TypeSpec const* spec,
                                  OUT Aggr ** s)
{
    if (is_struct(spec)) {
        return is_struct_exist_in_outer_scope(scope, tag, (Struct**)s);
    }
    ASSERT0(is_union(spec));
    return is_union_exist_in_outer_scope(scope, tag, (Union**)s);
}


//Return true if the struct typed declaration have already existed in both
//current and all of outer scopes.
bool is_struct_exist_in_outer_scope(Scope * scope, CHAR const* tag,
                                    OUT Struct ** s)
{
    ASSERT0(scope);
    Scope * sc = scope;
    while (sc != nullptr) {
        if (is_struct_type_exist(SCOPE_struct_list(sc), tag, s)) {
            return true;
        }
        sc = SCOPE_parent(sc);
    }
    return false;
}


//Return true if the struct typed declaration have already existed in both
//current and all of outer scopes.
bool is_struct_exist_in_outer_scope(Scope * scope, Sym const* tag,
                                    OUT Struct ** s)
{
    ASSERT0(scope);
    Scope * sc = scope;
    while (sc != nullptr) {
        if (is_struct_type_exist(SCOPE_struct_list(sc), tag, s)) {
            return true;
        }
        sc = SCOPE_parent(sc);
    }
    return false;
}


//Return true if the union typed declaration have already existed in both
//current and all of outer scopes.
bool is_union_exist_in_outer_scope(Scope * scope, CHAR const* tag,
                                   OUT Union ** s)
{
    Scope * sc = scope;
    while (sc != nullptr) {
        if (is_union_type_exist(SCOPE_union_list(sc), tag, s)) {
            return true;
        }
        sc = SCOPE_parent(sc);
    }
    return false;
}


//Return true if the union typed declaration have already existed in both
//current and all of outer scopes.
bool is_union_exist_in_outer_scope(Scope * scope, Sym const* tag,
                                   OUT Union ** s)
{
    Scope * sc = scope;
    while (sc != nullptr) {
        if (is_union_type_exist(SCOPE_union_list(sc), tag, s)) {
            return true;
        }
        sc = SCOPE_parent(sc);
    }
    return false;
}


//Return true if 'name' indicate an enum const which have already been
//defined in either current scope orand all of outer scopes.
//'name': enum name to be checked.
//'e': enum type set.
//'idx': index in 'e' const list, start at 0.
bool findEnumConst(CHAR const* name, OUT Enum ** e, OUT INT * idx)
{
    for (Scope * sc = g_cur_scope; sc != nullptr; sc = SCOPE_parent(sc)) {
        if (is_enum_exist(SCOPE_enum_list(sc), name, e, idx)) {
            return true;
        }
    }
    return false;
}


static bool is_enum_const_name_exist(Enum const* e,
                                     CHAR const* ev_name,
                                     OUT INT * idx)
{
    if (e == nullptr || ev_name == nullptr) { return false; }
    EnumValueList * evl = ENUM_vallist(e);
    INT i = 0;
    while (evl != nullptr) {
        if (strcmp(SYM_name(EVAL_LIST_name(evl)) , ev_name) == 0) {
            *idx = i;
            return true;
        }
        evl = EVAL_LIST_next(evl);
        i++;
    }
    return false;
}


//Return true if enum identifier existed.
static bool is_enum_id_exist(EnumList const* e_list,
                             CHAR const* e_id_name,
                             OUT Enum ** e)
{
    if (e_list == nullptr || e_id_name == nullptr) return false;
    EnumList const* el = e_list;
    while (el != nullptr) {
        Enum * tmp = ENUM_LIST_enum(el);
        if (ENUM_name(tmp) == nullptr) continue;
        if (strcmp(SYM_name(ENUM_name(tmp)), e_id_name) == 0) {
            *e = tmp;
            return true;
        }
        el = ENUM_LIST_next(el);
    }
    return false;
}


bool is_user_type_exist(UserTypeList const* ut_list,
                        CHAR const* ut_name,
                        OUT Decl ** decl)
{
    if (ut_list == nullptr || ut_name == nullptr) return false;

    UserTypeList const* utl = ut_list;
    while (utl != nullptr) {
        Decl * dcl = USER_TYPE_LIST_utype(utl);
        if (strcmp(SYM_name(get_decl_sym(dcl)),ut_name) == 0) {
            *decl = dcl;
            return true;
        }
        utl = USER_TYPE_LIST_next(utl);
    }
    return false;

}


bool is_struct_type_exist(List<Struct*> const& struct_list,
                          Sym const* tag,
                          OUT Struct ** s)
{
    if (tag == nullptr) { return false; }
    xcom::C<Struct*> * ct;
    for (Struct * st = struct_list.get_head(&ct);
         ct != nullptr; st = struct_list.get_next(&ct)) {        
        if (STRUCT_tag(st) == tag) {
            *s = st;
            return true;
        }
    }
    return false;
}


bool is_struct_type_exist(List<Struct*> const& struct_list,
                          CHAR const* tag,
                          OUT Struct ** s)
{
    if (tag == nullptr) { return false; }
    xcom::C<Struct*> * ct;
    for (Struct * st = struct_list.get_head(&ct);
         ct != nullptr; st = struct_list.get_next(&ct)) {
        Sym const* sym = STRUCT_tag(st);
        if (sym == nullptr) { continue; }
        if (::strcmp(SYM_name(sym), tag) == 0) {
            *s = st;
            return true;
        }
    }
    return false;
}


//Seach Union list accroding to the 'tag' of union-type.
bool is_union_type_exist(List<Union*> const& u_list,
                         CHAR const* tag,
                         OUT Union ** u)
{
    if (tag == nullptr) { return false; }
    xcom::C<Union*> * ct;
    for (Union * st = u_list.get_head(&ct);
         st != nullptr; st = u_list.get_next(&ct)) {
        Sym const* sym = UNION_tag(st);
        if (sym == nullptr) { continue; }
        if (::strcmp(SYM_name(sym), tag) == 0) {
            *u = st;
            return true;
        }
    }
    return false;
}


//Seach Union list accroding to the 'tag' of union-type.
bool is_union_type_exist(List<Union*> const& u_list,
                         Sym const* tag,
                         OUT Union ** u)
{
    if (tag == nullptr) { return false; }
    xcom::C<Union*> * ct;
    for (Union * st = u_list.get_head(&ct);
         st != nullptr; st = u_list.get_next(&ct)) {
        if (UNION_tag(st) == tag) {
            *u = st;
            return true;
        }
    }
    return false;
}


Enum * new_enum()
{
    return (Enum*)xmalloc(sizeof(Enum));
}


TypeSpec * new_type()
{
    TypeSpec * ty = (TypeSpec*)xmalloc(sizeof(TypeSpec));
    ty->clean();
    return ty;
}


TypeSpec * new_type(INT cate)
{
    TypeSpec * ty = (TypeSpec*)xmalloc(sizeof(TypeSpec));
    ty->clean();
    TYPE_des(ty) = cate;
    return ty;
}


//Compute array byte size.
//'decl' presents DCL_DECLARATOR or DCL_ABS_DECLARATOR,
//Compute byte size of total array.
static UINT computeArrayByteSize(TypeSpec const* spec, Decl const* decl)
{
    if (DECL_dt(decl) == DCL_DECLARATOR) {
        decl = DECL_child(decl);
        if (DECL_dt(decl) != DCL_ID) {
            err(g_src_line_num, "declarator absent identifier");
            return 0;
        }
        decl = DECL_next(decl);
    } else if (DECL_dt(decl) == DCL_ABS_DECLARATOR) {
        decl = DECL_child(decl);
    }
    if (DECL_dt(decl) == DCL_ID) {
        decl = DECL_next(decl);
    }
    if (decl == nullptr) {
        return 0;
    }

    UINT num = 0;
    UINT dim = 0;
    while (decl != nullptr && DECL_dt(decl) == DCL_ARRAY) {
        UINT dimsz = (UINT)DECL_array_dim(decl);
        if (dimsz == 0) {
            if (IS_EXTERN(spec)) {
                dimsz = 1;
            } else {
                err(g_src_line_num,
                    "size of %dth dimension can not be zero", dim);
                return 0;
            }
        }

        if (num == 0) {
            //Meet the first dim
            num = dimsz;
        } else {
            num *= dimsz;
        }

        dim++;
        decl = DECL_next(decl);
    }

    //Note host's UINT may be longer than target machine.
    ASSERTN(computeMaxBitSizeForValue(num) <= BYTE_PER_INT * BIT_PER_BYTE,
            ("too large array"));
    return num;
}


UINT computeScalarTypeBitSize(UINT des)
{
    ULONG bitsize = 0; //the max bit size the group hold.
    switch (des) {
    case T_SPEC_CHAR|T_SPEC_UNSIGNED:
    case T_SPEC_CHAR|T_SPEC_SIGNED:
    case T_SPEC_CHAR:
        bitsize = BYTE_PER_CHAR * BIT_PER_BYTE;
        break;
    case T_SPEC_SHORT|T_SPEC_UNSIGNED:
    case T_SPEC_SHORT|T_SPEC_SIGNED:
    case T_SPEC_SHORT:
        bitsize = BYTE_PER_SHORT * BIT_PER_BYTE;
        break;
    case T_SPEC_ENUM:
    case T_SPEC_INT|T_SPEC_UNSIGNED:
    case T_SPEC_INT|T_SPEC_SIGNED:
    case T_SPEC_INT:
    case T_SPEC_SIGNED:
    case T_SPEC_UNSIGNED:
        bitsize = BYTE_PER_INT * BIT_PER_BYTE;
        break;
    case T_SPEC_LONG|T_SPEC_UNSIGNED:
    case T_SPEC_LONG|T_SPEC_SIGNED:
    case T_SPEC_LONG:
        bitsize = BYTE_PER_LONG * BIT_PER_BYTE;
        break;
    case T_SPEC_LONGLONG|T_SPEC_UNSIGNED:
    case T_SPEC_LONGLONG|T_SPEC_SIGNED:
    case T_SPEC_LONGLONG:
        bitsize = BYTE_PER_LONGLONG * BIT_PER_BYTE;
        break;
    case T_SPEC_DOUBLE:
    case T_SPEC_DOUBLE|T_SPEC_LONG:
    case T_SPEC_DOUBLE|T_SPEC_LONGLONG:
        bitsize = BYTE_PER_DOUBLE * BIT_PER_BYTE;
        break;
    case T_SPEC_FLOAT:
    case T_SPEC_FLOAT|T_SPEC_LONG:
    case T_SPEC_FLOAT|T_SPEC_LONGLONG:
        bitsize = BYTE_PER_FLOAT * BIT_PER_BYTE;
        break;
    default: UNREACHABLE();
    }
    return bitsize;
}


//Return byte size of a group of bit fields that consisted of integer type.
UINT computeBitFieldByteSize(Decl const** dcl)
{
    ASSERTN(is_integer(*dcl), ("must be handled in struct_declarator()"));
    ASSERT0(DECL_spec(*dcl));

    //The integer type of the bit group.
    ULONG int_ty = TYPE_des(DECL_spec(*dcl));

    //the max bit size the group hold.
    ULONG int_bitsize = computeScalarTypeBitSize(int_ty);

    UINT bitsize = 0;
    UINT total_bitsize = int_bitsize;
    for (; (*dcl) != nullptr;) {
        TypeSpec * ty2 = DECL_spec(*dcl);
        ASSERT0(ty2);

        Decl const* declarator = get_declarator(*dcl);
        ASSERT0(declarator);

        if (!DECL_is_bit_field(declarator)) {
            break;
        }

        ASSERT0(DECL_bit_len(declarator) > 0);

        if (TYPE_des(ty2) != int_ty) { break; }

        if (bitsize + DECL_bit_len(declarator) > int_bitsize) {
            total_bitsize += int_bitsize;
            bitsize = 0;
        }

        bitsize += DECL_bit_len(declarator);

        *dcl = DECL_next(*dcl);
    }

    ASSERT0(total_bitsize != 0);
    return total_bitsize / BIT_PER_BYTE;
}


UINT computeAggrAlignedSize(Aggr const* aggr, UINT aggr_size,
                            UINT max_field_size)
{
    if (AGGR_align(aggr) < max_field_size) {
        //Ensure field alignment is compatible with target machine's
        //alignment constraint.
        max_field_size = pad_align(max_field_size, AGGR_align(aggr));
    }
    if (AGGR_pack_align(aggr) != 0) {
        aggr_size = pad_align(aggr_size, AGGR_align(aggr));
    } else {
        //There is no user declared constraint on struct alignment,
        //thus aligning the struct in its natural alignment.
        aggr_size = pad_align(aggr_size, max_field_size);
    }
    return aggr_size;
}


static UINT compute_field_ofst(Aggr const* s, UINT ofst,
                               Decl const* dcl, UINT field_align,
                               UINT * elem_bytesize)
{
    if (is_array(dcl)) {
        Decl const* elem_dcl = get_array_base_decl(dcl);
        *elem_bytesize = get_decl_size(elem_dcl);
        UINT elem_num = get_array_elemnum(dcl);
        ofst = compute_field_ofst_consider_pad(s, ofst, *elem_bytesize,
                                               elem_num, AGGR_field_align(s));
    } else {
        *elem_bytesize = get_decl_size(dcl);
        ofst = compute_field_ofst_consider_pad(s, ofst, *elem_bytesize,
                                               1, AGGR_field_align(s));
    }
    return ofst;
}


static UINT computeStructTypeSize(TypeSpec const* ty)
{
    ASSERT0(IS_STRUCT(ty));
    ASSERT0(is_struct_complete(ty));
    Struct const* s = TYPE_struct_type(ty);
    Decl const* dcl = STRUCT_decl_list(s);
    UINT ofst = 0;
    UINT max_field_sz = 0;
    while (dcl != nullptr) {
        if (is_bitfield(dcl)) {
            UINT bytesize = computeBitFieldByteSize(&dcl);
            ofst = compute_field_ofst_consider_pad(s, ofst, bytesize, 1,
                                                   AGGR_field_align(s));
            max_field_sz = MAX(max_field_sz, bytesize);
            continue;
        }
        UINT elem_bytesize = 0;
        ofst = compute_field_ofst(s, ofst, dcl, AGGR_field_align(s),
                                  &elem_bytesize);
        max_field_sz = MAX(max_field_sz, elem_bytesize);
        dcl = DECL_next(dcl);
    }

    UINT size = ofst;
    return computeAggrAlignedSize(s, size, max_field_sz);
}


UINT computeUnionTypeSize(TypeSpec const* ty)
{
    ASSERT0(IS_UNION(ty));
    ASSERT0(is_union_complete(ty));
    Union const* s = TYPE_union_type(ty);
    Decl const* dcl = UNION_decl_list(s);
    UINT size = 0;
    while (dcl != nullptr) {
        size = MAX(size, get_decl_size(dcl));
        dcl = DECL_next(dcl);
    }
    return computeAggrAlignedSize(s, size, size);
}


//What is 'complex type'? Non-pointer and non-array type.
//e.g : int * a;
//      int a[];
bool is_complex_type(Decl const* dcl)
{
    dcl = get_pure_declarator(dcl);
    if (dcl == nullptr) { return false; }
    return (is_pointer(dcl) || is_array(dcl));
}


//Get specifior type, specifior type refers to Non-pointer and non-array type.
// e.g: int a;
//      void a;
//      struct a;
//      union a;
//      enum a;
//      USER_DEFINED_TYPE_NAME a;
UINT getSpecTypeSize(TypeSpec const* spec)
{
    if (spec == nullptr) { return 0; }
    if (HAVE_FLAG(TYPE_des(spec), T_SPEC_LONGLONG)) return BYTE_PER_LONGLONG;
    if (HAVE_FLAG(TYPE_des(spec), T_SPEC_VOID)) return BYTE_PER_CHAR;
    if (HAVE_FLAG(TYPE_des(spec), T_SPEC_CHAR)) return BYTE_PER_CHAR;
    if (HAVE_FLAG(TYPE_des(spec), T_SPEC_BOOL)) return BYTE_PER_CHAR;
    if (HAVE_FLAG(TYPE_des(spec), T_SPEC_SHORT)) return BYTE_PER_SHORT;
    if (HAVE_FLAG(TYPE_des(spec), T_SPEC_INT)) return BYTE_PER_INT;
    if (HAVE_FLAG(TYPE_des(spec), T_SPEC_LONG)) return BYTE_PER_LONG;
    if (HAVE_FLAG(TYPE_des(spec), T_SPEC_FLOAT)) return BYTE_PER_FLOAT;
    if (HAVE_FLAG(TYPE_des(spec), T_SPEC_DOUBLE)) return BYTE_PER_DOUBLE;
    if (HAVE_FLAG(TYPE_des(spec), T_SPEC_STRUCT)) {
        return computeStructTypeSize(spec);
    }
    if (HAVE_FLAG(TYPE_des(spec), T_SPEC_UNION)) {
        return computeUnionTypeSize(spec);
    }
    if (HAVE_FLAG(TYPE_des(spec), T_SPEC_ENUM)) return BYTE_PER_ENUM;
    if (HAVE_FLAG(TYPE_des(spec), T_SPEC_SIGNED)) return BYTE_PER_INT;
    if (HAVE_FLAG(TYPE_des(spec), T_SPEC_UNSIGNED)) return BYTE_PER_INT;
    return 0;
}


//Compute byte size to complex type.
//complex type means the type is either pointer or array.
//e.g : int * a;
//      int a [];
ULONG getComplexTypeSize(Decl const* decl)
{
    if (decl == nullptr) { return 0; }

    TypeSpec const* spec = DECL_spec(decl);
    Decl const* d = nullptr;
    if (DECL_dt(decl) == DCL_DECLARATION || DECL_dt(decl) == DCL_TYPE_NAME) {
        d = get_pure_declarator(decl);
        ASSERTN(d != nullptr, ("composing type expected decl-spec"));
    } else {
        err(g_src_line_num, "expected declaration or type-name");
        return 0;
    }

    ASSERTN(spec, ("composing type expected specifier"));
    ULONG declor_size = getDeclaratorSize(spec, d);
    if (is_array(d)) {
        Decl const* base_dcl = get_array_base_declarator(d);
        if (base_dcl != nullptr && DECL_dt(base_dcl) == DCL_POINTER) {
            //If base of array is pointer, then we can disgard the base type
            //of the pointer, because all pointer size is identical.
            return declor_size * BYTE_PER_POINTER;
        }

        //The base of array could only be pointer, what else could there be?
        ASSERT0(base_dcl == nullptr);
        ULONG s = getSpecTypeSize(spec);
        return declor_size * s;
    }

    ASSERT0(is_pointer(d));
    return declor_size;
}


//Construct TypeSpec-NAME declaration.
Decl const* gen_type_name(TypeSpec * ty)
{
    Decl * decl = new_decl(DCL_TYPE_NAME);
    DECL_decl_list(decl) = new_decl(DCL_ABS_DECLARATOR);
    DECL_spec(decl) = ty;
    return decl;
}


//Compute the byte size of declaration.
//This function will compute array size.
UINT get_decl_size(Decl const* decl)
{
    TypeSpec const* spec = DECL_spec(decl);
    if (DECL_dt(decl) == DCL_DECLARATION || DECL_dt(decl) == DCL_TYPE_NAME) {
        Decl const* d = DECL_decl_list(decl); //get declarator
        ASSERTN(d && (DECL_dt(d) == DCL_DECLARATOR ||
                      DECL_dt(d) == DCL_ABS_DECLARATOR),
                ("illegal declarator"));
        if (is_complex_type(d)) {
            return getComplexTypeSize(decl);
        }
        return getSpecTypeSize(spec);
    }
    ASSERTN(0, ("unexpected declaration"));    
    return 0;
}


//Get and generate array element declaration.
//Note if array is multiple-dimension, the funtion only construct and
//return the element type of sub-dimension.
//e.g: given int arr[10][20]; the declarator is: ID(arr)->ARRAY(10)->ARRAY(20).
//     The function constructs and returns an array decl: 'int [20]';
Decl * get_array_elem_decl(Decl const* decl)
{
    ASSERT0(is_array(decl));
    //Return sub-dimension of base if 'decl' is
    //multi-dimensional array.
    Decl * elemdcl = cp_decl_fully(decl);
    ASSERT0(PURE_DECL(elemdcl));
    Decl * td = PURE_DECL(elemdcl);
    if (DECL_dt(td) == DCL_ID) {
        //e.g: If td is: ID->ARRAY->ARRAY.
        //We elide ID and keep ARRAY->ARRAY.
        td = DECL_next(td);
    }
    if (DECL_dt(td) == DCL_ARRAY || DECL_dt(td) == DCL_POINTER) {
        //e.g: If td is: ARRAY->ARRAY.
        //We elide the first ARRAY, and keep the second ARRAY.
        xcom::remove(&PURE_DECL(elemdcl), td);
    }
    return elemdcl;
}


//Get and generate array base declaration.
//Note if array is multiple-dimension, the funtion constructs and
//return the basel type of sub-dimension.
//e.g: given int arr[10][20];
//     the function construct and return decl: 'int';
Decl * get_array_base_decl(Decl const* decl)
{
    ASSERT0(is_array(decl));
    //Return sub-dimension of base if 'decl' is
    //multi-dimensional array.
    Decl * newdecl = cp_decl_fully(decl);
    ASSERT0(PURE_DECL(newdecl));
    Decl * td = PURE_DECL(newdecl);

    //Given declator list, we elide the most outside ARRAY, and keep the rest
    //and the declator of base type.
    //e.g: If td is: ID->ARRAY1->POINTER->ARRAY2->ARRAY3.
    //  We elide ARRAY1, the returned declator
    //  is: ID->POINTER->ARRAY2->ARRAY3.
    Decl * dclor = get_first_array_decl(newdecl);
    Decl * prev = nullptr;
    while (dclor != nullptr && DECL_dt(dclor) == DCL_ARRAY) {
        prev = DECL_prev(dclor);
        xcom::remove(&PURE_DECL(newdecl), dclor);
        dclor = prev;
    }

    return newdecl;
}


//Return the *first* Decl structure which indicate an array
//in pure-list of declaration.
//e.g: int p[10][20]; the declarator is: DCL_ID(p)->DCL_ARRAY(20)->DCL_ARRAY(10).
//return DCL_ARRAY(20).
Decl * get_first_array_decl(Decl * decl)
{
    ASSERTN(DECL_dt(decl) == DCL_TYPE_NAME || DECL_dt(decl) == DCL_DECLARATION ,
            ("expect DCRLARATION"));
    ASSERTN(is_array(decl), ("expect pointer type"));
    Decl * x = const_cast<Decl*>(get_pure_declarator(decl));
    while (x != nullptr) {
        switch (DECL_dt(x)) {
        case DCL_FUN:
        case DCL_POINTER:
            return nullptr;
        case DCL_ARRAY:
            return x;
        case DCL_ID:
        case DCL_VARIABLE:
            break;
        default:
            ASSERTN(DECL_dt(x) != DCL_DECLARATION &&
                   DECL_dt(x) != DCL_DECLARATOR &&
                   DECL_dt(x) != DCL_ABS_DECLARATOR &&
                   DECL_dt(x) != DCL_TYPE_NAME,
                   ("\nunsuitable Decl type locate here in is_pointer()\n"));
            return nullptr;
        }
        x = DECL_next(x);
    }
    return nullptr;
}


//Return the *first* Decl structure which indicate a pointer
//in pure-list of declaration.
//e.g: int * p; the declarator is: DCL_ID(p)->DCL_POINTER(*).
//return DCL_POINTER.
Decl const* get_pointer_decl(Decl const* decl)
{
    ASSERTN(DECL_dt(decl) == DCL_TYPE_NAME ||
            DECL_dt(decl) == DCL_DECLARATION ,
            ("expect DCRLARATION"));
    ASSERTN(is_pointer(decl), ("expect pointer type"));
    Decl const* x = get_pure_declarator(decl);
    while (x != nullptr) {
        switch (DECL_dt(x)) {
        case DCL_FUN:
            //function-pointer type:
            //    DCL_ID->POINTER->DCL_FUN
            //function-decl type:
            //    DCL_ID->DCL_FUN
            if (DECL_prev(x) != nullptr &&
                DECL_dt(DECL_prev(x)) == DCL_POINTER) {
                return DECL_prev(x);
            }
            return nullptr;
        case DCL_POINTER:
            return x;
        case DCL_ID:
        case DCL_VARIABLE:
            break;
        default:
            ASSERTN(DECL_dt(x) != DCL_DECLARATION &&
                    DECL_dt(x) != DCL_DECLARATOR &&
                    DECL_dt(x) != DCL_ABS_DECLARATOR &&
                    DECL_dt(x) != DCL_TYPE_NAME,
                    ("\nunsuitable Decl type locate here in is_pointer()\n"));
            return nullptr;
        }
        x = DECL_next(x);
    }
    return nullptr;
}


//Get base type of POINTER.
Decl * get_pointer_base_decl(Decl const* decl, TypeSpec ** ty)
{
    ASSERTN(DECL_dt(decl) == DCL_TYPE_NAME || DECL_dt(decl) == DCL_DECLARATION,
            ("expect DCRLARATION"));
    ASSERTN(is_pointer(decl), ("expect pointer type"));

    if (ty != nullptr) {
        *ty = DECL_spec(decl);
    }

    Decl * d = const_cast<Decl*>(get_pure_declarator(decl));
    if (DECL_dt(d) == DCL_ID) {
        d = DECL_next(d);
        ASSERTN(DECL_dt(d) == DCL_POINTER || DECL_dt(d) == DCL_FUN,
                ("expect pointer declarator"));
        return DECL_next(d); //get Decl that is the heel of '*'
    } else if (DECL_dt(d) == DCL_POINTER || DECL_dt(d) == DCL_FUN) {
        return DECL_next(d); //get Decl that is the heel of '*'
    }

    ASSERTN(0, ("it is not a pointer type"));
    return nullptr;
}


//Compute byte size of pointer base declarator.
//e.g: Given 'int *(*p)[3]', the pointer-base is 'int * [3]'.
UINT get_pointer_base_size(Decl const* decl)
{
    ASSERT0(DECL_dt(decl) == DCL_DECLARATION ||
            DECL_dt(decl) == DCL_TYPE_NAME);
    TypeSpec * ty = nullptr;
    Decl * d = get_pointer_base_decl(decl, &ty);
    if (d == nullptr) {
        //base type of pointer oughts to be TypeSpec-SPEC.
        if (ty != nullptr &&
            ((is_struct(ty) && !is_struct_complete(ty)) ||
             (is_union(ty) && !is_union_complete(ty)))) {
            //The struct/union is incomplete.
            return 0;
        }

        UINT s = getSpecTypeSize(ty);
        ASSERTN(s != 0, ("simply type size cannot be zero"));
        return s;
    }

    UINT s = 1;
    UINT e = getDeclaratorSize(DECL_spec(decl), d);
    if (!is_pointer(d)) {
        s = getSpecTypeSize(ty);
    }
    ASSERTN(e != 0, ("declarator size cannot be zero"));
    return e * s;
}


bool is_simple_base_type(TypeSpec const* ty)
{
    if (ty == nullptr) { return false; }
    return (TYPE_des(ty) & T_SPEC_VOID ||
            TYPE_des(ty) & T_SPEC_CHAR ||
            TYPE_des(ty) & T_SPEC_SHORT ||
            TYPE_des(ty) & T_SPEC_INT ||
            TYPE_des(ty) & T_SPEC_LONGLONG ||
            TYPE_des(ty) & T_SPEC_LONG ||
            TYPE_des(ty) & T_SPEC_FLOAT ||
            TYPE_des(ty) & T_SPEC_DOUBLE ||
            TYPE_des(ty) & T_SPEC_SIGNED ||
            TYPE_des(ty) & T_SPEC_UNSIGNED);
}


bool is_simple_base_type(INT des)
{
    return (des & T_SPEC_VOID ||
            des & T_SPEC_ENUM ||
            des & T_SPEC_CHAR ||
            des & T_SPEC_SHORT ||
            des & T_SPEC_INT ||
            des & T_SPEC_LONGLONG ||
            des & T_SPEC_LONG ||
            des & T_SPEC_FLOAT ||
            des & T_SPEC_DOUBLE ||
            des & T_SPEC_SIGNED ||
            des & T_SPEC_UNSIGNED);
}


static INT format_base_type_spec(StrBuf & buf, TypeSpec const* ty)
{
    if (ty == nullptr) { return ST_SUCC; }
    if (!is_simple_base_type(ty)) {
        return ST_ERR;
    }
    if (TYPE_des(ty) & T_SPEC_SIGNED)
        buf.strcat("signed ");
    if (TYPE_des(ty) & T_SPEC_UNSIGNED)
        buf.strcat("unsigned ");
    if (TYPE_des(ty) & T_SPEC_CHAR)
        buf.strcat("char ");
    if (TYPE_des(ty) & T_SPEC_SHORT)
        buf.strcat("short ");
    if (TYPE_des(ty) & T_SPEC_LONG)
        buf.strcat("long ");
    if (TYPE_des(ty) & T_SPEC_INT)
        buf.strcat("int ");
    if (TYPE_des(ty) & T_SPEC_LONGLONG)
        buf.strcat("longlong ");
    if (TYPE_des(ty) & T_SPEC_FLOAT)
        buf.strcat("float ");
    if (TYPE_des(ty) & T_SPEC_DOUBLE)
        buf.strcat("double ");
    if (TYPE_des(ty) & T_SPEC_VOID)
        buf.strcat("void ");
    return ST_SUCC;

}


INT format_enum_complete(StrBuf & buf, Enum const* e)
{
    if (e == nullptr) { return ST_SUCC; }
    //CHAR * p = buf + strlen(buf);
    if (ENUM_name(e)) {
        buf.strcat("%s ", SYM_name(ENUM_name(e)));
        //p = p + strlen(p);
    }
    if (ENUM_vallist(e)) {
        buf.strcat("{");
        EnumValueList * ev = ENUM_vallist(e);
        while (ev != nullptr) {
            //p = p + strlen(p);
            buf.strcat("%s ", SYM_name(EVAL_LIST_name(ev)));
            ev = EVAL_LIST_next(ev);
        }
        buf.strcat("} ");
    }
    return ST_SUCC;
}


static INT format_enum_complete(StrBuf & buf, TypeSpec const* ty)
{
    if (ty == nullptr) { return ST_SUCC; }
    buf.strcat("enum ");
    format_enum_complete(buf, TYPE_enum_type(ty));
    return ST_SUCC;
}


//Format union's name and members.
INT format_union_complete(StrBuf & buf, Union const* u)
{
    if (u == nullptr) { return ST_SUCC; }
    buf.strcat("union ");
    Decl * member = UNION_decl_list(u);
    if (UNION_tag(u)) {
        buf.strcat(SYM_name(UNION_tag(u)));
    }

    //Printf members.
    buf.strcat("{");
    while (member != nullptr) {
        format_declaration(buf, member);
        buf.strcat("; ");
        member = DECL_next(member);
    }
    buf.strcat("}");
    return ST_SUCC;
}


//Format struct's name and members.
INT format_struct_complete(StrBuf & buf, Struct const* s)
{
    if (s == nullptr) { return ST_SUCC; }
    buf.strcat("struct ");
    Decl * member = STRUCT_decl_list(s);
    if (STRUCT_tag(s)) {
        buf.strcat(SYM_name(STRUCT_tag(s)));
    }

    //Printf members.
    buf.strcat("{");
    while (member != nullptr) {
        ASSERT0(DECL_dt(member) == DCL_DECLARATION);
        if ((is_struct(member) || is_union(member)) &&
            TYPE_struct_type(DECL_spec(member)) == s) {
            //It will be recursive definition of struction/union,
            //e.g:
            //struct S {
            //    struct S {
            //        ...
            //    }
            //}
            ASSERT0(is_pointer(member));
        }
        format_declaration(buf, member);
        buf.strcat("; ");
        member = DECL_next(member);
    }
    buf.strcat("}");
    return ST_SUCC;
}


//Format struct/union's name and members.
INT format_struct_union_complete(StrBuf & buf, TypeSpec const* ty)
{
    if (ty == nullptr) { return ST_SUCC; }
    format_struct_union(buf, ty);

    //Printf members.
    Struct * s = TYPE_struct_type(ty);
    if (s == nullptr) { return ST_SUCC; }

    Decl * member = STRUCT_decl_list(s);
    buf.strcat("{");
    while (member != nullptr) {
        format_declaration(buf, member);
        buf.strcat("; ");
        member = DECL_next(member);
    }
    buf.strcat("}");
    return ST_SUCC;
}


//Format struct/union's name, it does not include members.
static INT format_struct_union(StrBuf & buf, TypeSpec const* ty)
{
    if (ty == nullptr) { return ST_SUCC; }
    if (TYPE_des(ty) & T_SPEC_STRUCT) {
        buf.strcat("struct ");
    } else if (TYPE_des(ty) & T_SPEC_UNION) {
        buf.strcat("union ");
    } else {
        err(g_src_line_num, "expected a struct or union");
        return ST_ERR;
    }

    Struct * s = TYPE_struct_type(ty);

    //Illegal type, TYPE_struct_type can not be nullptr,
    //one should filter this case before invoke format_struct_union();
    ASSERT0(s);

    //printf tag
    if (STRUCT_tag(s)) {
        //CHAR * p = buf + strlen(buf);
        buf.strcat("%s ", SYM_name(STRUCT_tag(s)));
    }
    return ST_SUCC;
}


static INT format_stor_spec(StrBuf & buf, TypeSpec const* ty)
{
    if (ty == nullptr) { return ST_SUCC; }
    if (IS_REG(ty)) buf.strcat("register ");
    if (IS_STATIC(ty)) buf.strcat("static ");
    if (IS_EXTERN(ty)) buf.strcat("extern ");
    if (IS_TYPEDEF(ty)) buf.strcat("typedef ");
    return ST_SUCC;
}


INT format_quan_spec(StrBuf & buf, TypeSpec const* ty)
{
    if (ty == nullptr) { return ST_SUCC; }
    if (IS_CONST(ty)) buf.strcat("const ");
    if (IS_VOLATILE(ty)) buf.strcat("volatile ");
    return ST_SUCC;
}


INT format_decl_spec(StrBuf & buf, TypeSpec const* ty, bool is_ptr)
{
    if (ty == nullptr) { return ST_SUCC; }
    BYTE is_su = (BYTE)(IS_STRUCT(ty) || IS_UNION(ty)),
         is_enum = (BYTE)IS_ENUM_TYPE(ty) ,
         is_base = (is_simple_base_type(ty)) != 0 ,
         is_ut = (BYTE)IS_USER_TYPE_REF(ty);
    format_stor_spec(buf, ty);
    format_quan_spec(buf, ty);
    if (is_su) {
        if (is_ptr) {
            format_struct_union(buf, ty);
        } else {
            format_struct_union_complete(buf, ty);
        }
        return ST_SUCC;
    } else if (is_enum) {
        return format_enum_complete(buf, ty);
    } else if (is_base) {
        return format_base_type_spec(buf, ty);
    } else if (is_ut) {
        return format_user_type_spec(buf, ty);
    }
    return ST_ERR;
}


INT format_parameter_list(StrBuf & buf, Decl const* decl)
{
    if (decl == nullptr) { return ST_SUCC; }
    while (decl != nullptr) {
        format_declaration(buf, decl);
        buf.strcat(",");
        decl = DECL_next(decl);
    }
    return ST_SUCC;
}


static INT format_dcrl_reverse(StrBuf & buf,
                               TypeSpec const* ty,
                               Decl const* decl)
{
    if (decl == nullptr) { return ST_SUCC; }
    switch (DECL_dt(decl)) {
    case DCL_POINTER: {
        TypeSpec * quan = DECL_qua(decl);
        bool blank = false;
        if (quan != nullptr) {
            if (IS_CONST(quan)) {
                buf.strcat("const ");
                blank = true;
            }
            if (IS_VOLATILE(quan)) {
                buf.strcat("volatile ");
                blank = true;
            }
            if (IS_RESTRICT(quan)) {
                buf.strcat("restrict ");
                blank = true;
            }
        }
        if (!blank) { buf.strcat(" "); }
        buf.strcat("* ");
        if (DECL_is_paren(decl)) {
            buf.strcat("(");
            format_dcrl_reverse(buf, ty, DECL_prev(decl));
            buf.strcat(")");
        } else {
            format_dcrl_reverse(buf, ty, DECL_prev(decl));
        }
        break;
    }
    case DCL_ID: {
        Tree * t = DECL_id(decl);
        TypeSpec * quan = DECL_qua(decl);
        bool blank = false;
        if (quan != nullptr) {
            ASSERT0(!IS_RESTRICT(quan));                
            if (IS_VOLATILE(quan)) {
                buf.strcat("volatile ");
                blank = true;
            }
            if (IS_CONST(quan)) {
                buf.strcat("const ");
                blank = true;
            }
        }
        if (!blank) { buf.strcat(" "); }
        buf.strcat("%s ", SYM_name(TREE_id(t)));
        if (DECL_is_paren(decl)) {
            buf.strcat("(");
            //p = p + strlen(p);
            format_dcrl_reverse(buf, ty, DECL_prev(decl));
            buf.strcat(")");
        } else {
            format_dcrl_reverse(buf, ty, DECL_prev(decl));
        }
        break;
    }
    case DCL_FUN:
        if (DECL_prev(decl) != nullptr &&
            DECL_dt(DECL_prev(decl)) == DCL_POINTER) {
            //FUN_POINTER
            buf.strcat("(");
            format_dcrl_reverse(buf, ty, DECL_prev(decl));
            buf.strcat(")");
        } else {
            //FUN_DECL
            format_dcrl_reverse(buf, ty, DECL_prev(decl));
        }
        buf.strcat("(");
        format_parameter_list(buf, DECL_fun_para_list(decl));
        buf.strcat(")");
        break;
    case DCL_ARRAY: {
        if (DECL_is_paren(decl)) {
            buf.strcat("(");
            format_dcrl_reverse(buf, ty, DECL_prev(decl));
            buf.strcat(")");
        } else {
            format_dcrl_reverse(buf, ty, DECL_prev(decl));
        }
        //bound of each dimensions should be computed while
        //the DECLARATION parsed.
        LONGLONG v = DECL_array_dim(decl);
        if (ty != nullptr && IS_EXTERN(ty) && v == 0) {
            //Set extern array to own one elemement at least.
            UNREACHABLE(); //should be handled in declaration()
        }
        //CHAR * p = buf + strlen(buf);
        buf.strcat("[%lld]", v);
        break;
    }
    default: ASSERTN(0, ("unknown Decl type"));
    }
    return ST_SUCC;
}


INT format_declarator(StrBuf & buf, TypeSpec const* ty, Decl const* decl)
{
    CHAR b[128];
    b[0] = 0;
    if (decl == nullptr) { return ST_SUCC; }
    if (DECL_dt(decl) == DCL_ABS_DECLARATOR||
        DECL_dt(decl) == DCL_DECLARATOR) {
        if (DECL_bit_len(decl)) {
            SNPRINTF(b, 128, ":%d", DECL_bit_len(decl));
        }
        decl = DECL_child(decl);
    }
    if (decl != nullptr) {
        ASSERTN((DECL_dt(decl) == DCL_ARRAY ||
            DECL_dt(decl) == DCL_POINTER ||
            DECL_dt(decl) == DCL_FUN     ||
            DECL_dt(decl) == DCL_ID      ||
            DECL_dt(decl) == DCL_VARIABLE),
            ("unknown declarator"));

        while (DECL_next(decl) != nullptr) { decl = DECL_next(decl); }
        format_dcrl_reverse(buf, ty, decl);
        buf.strcat(b);
    }
    return ST_SUCC;
}


INT format_user_type_spec(StrBuf & buf, TypeSpec const* ty)
{
    if (ty == nullptr) { return ST_SUCC; }
    ASSERT0(HAVE_FLAG(TYPE_des(ty), T_SPEC_USER_TYPE));
    Decl * ut = TYPE_user_type(ty);
    ASSERT0(ut != nullptr);
    return format_user_type_spec(buf, ut);
}


INT format_user_type_spec(StrBuf & buf, Decl const* ut)
{
    if (ut == nullptr) { return ST_SUCC; }
    return format_declaration(buf, ut);
}


INT format_declaration(StrBuf & buf, Decl const* decl)
{
    if (decl == nullptr) { return ST_SUCC; }
    if (DECL_dt(decl) == DCL_DECLARATION ||
        DECL_dt(decl) == DCL_TYPE_NAME) {
        TypeSpec * ty = DECL_spec(decl);
        Decl * dcl = DECL_decl_list(decl);
        format_decl_spec(buf, ty, is_pointer(decl));
        format_declarator(buf, ty, dcl);
        return ST_SUCC;
    } else if (DECL_dt(decl) == DCL_DECLARATOR ||
               DECL_dt(decl) == DCL_ABS_DECLARATOR) {
        Decl * dcl = DECL_decl_list(decl);
        buf.strcat("nullptr ");
        format_declarator(buf, nullptr, dcl);
    } else if (DECL_dt(decl) == DCL_POINTER ||
               DECL_dt(decl) == DCL_ARRAY ||
               DECL_dt(decl) == DCL_FUN ||
               DECL_dt(decl) == DCL_ID) {
        buf.strcat("nullptr ");
        format_declarator(buf, nullptr, decl);
    } else if (DECL_dt(decl) == DCL_VARIABLE) {
        buf.strcat("...");
    } else {
        ASSERTN(0, ("Unkonwn Decl type"));
    }
    return ST_ERR;
}


INT format_parameter_list(Decl const* decl, INT indent)
{
    if (decl == nullptr) { return ST_SUCC; }
    while (decl != nullptr) {
        format_declaration(decl, indent);
        decl = DECL_next(decl);
        if (decl != nullptr) prt(g_logmgr, ", \n");
    }
    return ST_SUCC;
}


INT format_dcrl(Decl const* decl, INT indent)
{
    if (decl == nullptr) {
        return ST_SUCC;
    }
    switch (DECL_dt(decl)) {
    case DCL_POINTER:
        {
            TypeSpec * quan = DECL_qua(decl);
            if (quan != nullptr) {
                if (IS_CONST(quan)) prt(g_logmgr, "const ");
                if (IS_VOLATILE(quan)) prt(g_logmgr, "volatile ");
                if (IS_RESTRICT(quan)) prt(g_logmgr, "restrict ");
            }
            if (DECL_next(decl) != nullptr) {
                if (DECL_dt(DECL_next(decl)) != DCL_FUN) {
                    prt(g_logmgr, "POINTER");
                    prt(g_logmgr, " -> ");
                }
                format_dcrl(DECL_next(decl), indent);
            } else {
                prt(g_logmgr, "POINTER");
            }
        }
        break;
    case DCL_ID:
        {
            Tree * t = DECL_id(decl);
            TypeSpec * quan = DECL_qua(decl);
            if (quan != nullptr) {
                ASSERT0(!IS_RESTRICT(quan));
                if (IS_CONST(quan)) prt(g_logmgr, "const ");
                if (IS_VOLATILE(quan)) prt(g_logmgr, "volatile ");
            }
            prt(g_logmgr, "ID:'%s'", SYM_name(TREE_id(t)));
            if (DECL_next(decl) != nullptr) { prt(g_logmgr, " -> ");    }
            format_dcrl(DECL_next(decl), indent);
        }
        break;
    case DCL_FUN:
        if (DECL_prev(decl) != nullptr &&
            DECL_dt(DECL_prev(decl)) == DCL_POINTER) {
            prt(g_logmgr, "FUN_POINTER");
        } else {
            prt(g_logmgr, "FUN_DECL");
        }

        if (DECL_fun_para_list(decl) == nullptr) {
            prt(g_logmgr, ",PARAM:()\n");
        } else {
            prt(g_logmgr, ",PARAM:(");
            format_parameter_list(DECL_fun_para_list(decl),
                                  indent + DECL_FMT_INDENT_INTERVAL);
            prt(g_logmgr, ")\n");
        }        
        if (DECL_next(decl) != nullptr) {
            prt(g_logmgr, " RET_VAL_DCL_TYPE:");
        }
        format_dcrl(DECL_next(decl), indent);
        break;
    case DCL_ARRAY:
        {
            prt(g_logmgr, "ARRAY");
            //bound of each dimensions should be computed while
            //the DECLARATION parsed.
            LONGLONG v = DECL_array_dim(decl);
            prt(g_logmgr, "[%lld]", v);
            if (DECL_next(decl) != nullptr) { prt(g_logmgr, " -> ");    }
            if (DECL_is_paren(decl)) {
                //prt(g_logmgr, "(");
                format_dcrl(DECL_next(decl), indent);
                //prt(g_logmgr, ")");
            } else {
                format_dcrl(DECL_next(decl), indent);
            }
            break;
        }
    default:
        ASSERTN(0, ("unknown Decl type"));
    }
    return ST_SUCC;
}


INT format_declarator(Decl const* decl, TypeSpec const* ty, INT indent)
{
    DUMMYUSE(ty);
    if (decl == nullptr) { return ST_SUCC; }

    if (DECL_dt(decl) == DCL_ABS_DECLARATOR||
        DECL_dt(decl) == DCL_DECLARATOR) {
        prt(g_logmgr, "%s", g_dcl_name[DECL_dt(decl)]);
        #ifdef _DEBUG_
        prt(g_logmgr, "(uid:%d)", DECL_uid(decl));
        #endif
        if (DECL_bit_len(decl)) {
            prt(g_logmgr, ",bitfield:%d", DECL_bit_len(decl));
        }
        note(g_logmgr, "\n");
        decl = DECL_child(decl);
    }

    if (decl != nullptr) {
        ASSERTN((DECL_dt(decl) == DCL_ARRAY ||
                 DECL_dt(decl) == DCL_POINTER ||
                 DECL_dt(decl) == DCL_FUN     ||
                 DECL_dt(decl) == DCL_ID      ||
                 DECL_dt(decl) == DCL_VARIABLE),
                 ("unknown declarator"));        
        g_logmgr->incIndent(DECL_FMT_INDENT_INTERVAL);
        format_dcrl(decl, indent + DECL_FMT_INDENT_INTERVAL);
        g_logmgr->decIndent(DECL_FMT_INDENT_INTERVAL);
    }
    return ST_SUCC;
}


INT format_user_type_spec(TypeSpec const* ty, INT indent)
{
    if (ty == nullptr) {
        return ST_SUCC;
    }
    if ((TYPE_des(ty) & T_SPEC_USER_TYPE) == 0) {
        return ST_ERR;
    }
    Decl * ut = TYPE_user_type(ty);
    return format_user_type_spec(ut, indent);
}


INT format_user_type_spec(Decl const* ut, INT indent)
{
    if (ut == nullptr) {
        return ST_SUCC;
    }
    return format_declaration(ut, indent);
}


INT format_declaration(Decl const* decl, INT indent)
{
    if (decl == nullptr || g_logmgr == nullptr) { return ST_SUCC; }
    note(g_logmgr, "\n");

    StrBuf sbuf(128);
    if (DECL_dt(decl) == DCL_DECLARATION || DECL_dt(decl) == DCL_TYPE_NAME) {
        TypeSpec * ty = DECL_spec(decl);
        Decl * dcl = DECL_decl_list(decl);
        prt(g_logmgr, "%s", g_dcl_name[DECL_dt(decl)]);
        #ifdef _DEBUG_
        prt(g_logmgr, "(uid:%d)", DECL_uid(decl));
        #endif
        prt(g_logmgr, "(line:%d)", DECL_lineno(decl));
        note(g_logmgr, "\n");

        format_decl_spec(sbuf, ty, is_pointer(decl));

        g_logmgr->incIndent(DECL_FMT_INDENT_INTERVAL);

        prt(g_logmgr, "SPECIFIER:%s", sbuf.buf);

        note(g_logmgr, "\n");
        format_declarator(dcl, DECL_spec(decl),
                          indent + DECL_FMT_INDENT_INTERVAL);

        g_logmgr->decIndent(DECL_FMT_INDENT_INTERVAL);

        return ST_SUCC;
    }
    
    if (DECL_dt(decl) == DCL_DECLARATOR ||
        DECL_dt(decl) == DCL_ABS_DECLARATOR) {
        Decl * dcl = DECL_decl_list(decl);
        prt(g_logmgr, "%s", g_dcl_name[DECL_dt(decl)]);
        note(g_logmgr, "\n");
        format_declarator(dcl, nullptr, indent + DECL_FMT_INDENT_INTERVAL);
        return ST_SUCC;
    }

    if (DECL_dt(decl) == DCL_POINTER ||
        DECL_dt(decl) == DCL_ARRAY ||
        DECL_dt(decl) == DCL_FUN ||
        DECL_dt(decl) == DCL_ID) {
        prt(g_logmgr, "%s ", g_dcl_name[DECL_dt(decl)]);
        format_declarator(decl, nullptr, indent + DECL_FMT_INDENT_INTERVAL);
        return ST_SUCC;
    }
    
    if (DECL_dt(decl) == DCL_VARIABLE) {
        prt(g_logmgr, "... ");
        return ST_SUCC;
    }

    ASSERTN(0, ("Unkonwn Decl type"));
    return ST_ERR;
}
//END DECL_FMT


//Fetch const value of 't' refered
INT get_enum_const_val(Enum const* e, INT idx)
{
    if (e == nullptr) { return -1; }

    EnumValueList const* evl = ENUM_vallist(e);
    while (idx > 0 && evl != nullptr) {
        evl = EVAL_LIST_next(evl);
        idx--;
    }

    if (evl == nullptr) {
        err(g_src_line_num, "enum const No.%d is not exist", idx);
        return -1;
    }

    return EVAL_LIST_val(evl);
}


//Fetch const value of 't' refered
CHAR const* get_enum_const_name(Enum const* e, INT idx)
{
    if (e == nullptr) { return nullptr; }

    EnumValueList * evl = ENUM_vallist(e);
    while (idx > 0 && evl != nullptr) {
        evl = EVAL_LIST_next(evl);
        idx--;
    }

    if (evl == nullptr) {
        err(g_src_line_num, "enum const No.%d is not exist", idx);
        return nullptr;
    }

    return SYM_name(EVAL_LIST_name(evl));
}


//If type is a user-defined type, return the actually type-spec.
TypeSpec * get_pure_type_spec(TypeSpec * type)
{
    ASSERT0(type);
    Decl * utdcl;
    if (IS_USER_TYPE_REF(type)) {
        utdcl = TYPE_user_type(type);
        return get_pure_type_spec(DECL_spec(utdcl));
    }
    return type;
}


bool is_bitfield(Decl const* decl)
{
    decl = get_declarator(decl);
    return decl != nullptr && DECL_is_bit_field(decl);
}


bool is_struct(TypeSpec const* type)
{
    type = get_pure_type_spec(const_cast<TypeSpec*>(type));
    return type != nullptr && IS_STRUCT(type);
}


bool is_struct(Decl const* decl)
{
    ASSERTN(decl &&
            (DECL_dt(decl) == DCL_TYPE_NAME ||
             DECL_dt(decl) == DCL_DECLARATION),
            ("need TypeSpec-NAME or DCRLARATION"));
    if (is_pointer(decl) || is_array(decl)) {
        //Complex type is consist of type-specifier and declarator.
        return false;
    }
    return is_struct(DECL_spec(decl));
}


CHAR const* get_aggr_type_name(TypeSpec const* type)
{
    return is_struct(type) ? "struct" : "union";
}


bool is_aggr(TypeSpec const* type)
{
    type = get_pure_type_spec(const_cast<TypeSpec*>(type));
    return (type != nullptr) && (IS_STRUCT(type) || IS_UNION(type));
}


bool is_aggr(Decl const* decl)
{
    return is_struct(decl) || is_union(decl);
}


bool is_union(TypeSpec const* type)
{
    type = get_pure_type_spec(const_cast<TypeSpec*>(type));
    return type != nullptr && IS_UNION(type);
}


bool is_union(Decl const* decl)
{
    ASSERTN(decl &&
            (DECL_dt(decl) == DCL_TYPE_NAME ||
             DECL_dt(decl) == DCL_DECLARATION),
            ("need TypeSpec-NAME or DCRLARATION"));
    if (is_pointer(decl) || is_array(decl)) {
        //Complex type is consist of type-specifier and declarator.
        return false;
    }
    return is_union(DECL_spec(decl));
}


//Is float-point.
bool is_fp(Decl const* dcl)
{
    ASSERTN(dcl &&
            (DECL_dt(dcl) == DCL_TYPE_NAME || DECL_dt(dcl) == DCL_DECLARATION),
            ("expect type-name or dcrlaration"));
    return is_fp(DECL_spec(dcl));
}


//Is single decision float-point.
bool is_float(Decl const* dcl)
{
    ASSERTN(dcl &&
            (DECL_dt(dcl) == DCL_TYPE_NAME || DECL_dt(dcl) == DCL_DECLARATION),
            ("expect type-name or dcrlaration"));
    return IS_TYPE(DECL_spec(dcl), T_SPEC_FLOAT);
}


//Is double decision float-point.
bool is_double(Decl const* dcl)
{
    ASSERTN(dcl &&
           (DECL_dt(dcl) == DCL_TYPE_NAME || DECL_dt(dcl) == DCL_DECLARATION),
           ("expect type-name or dcrlaration"));
    return IS_TYPE(DECL_spec(dcl), T_SPEC_DOUBLE);
}


//Is float-point.
bool is_fp(TypeSpec const* ty)
{
    return (IS_TYPE(ty, T_SPEC_FLOAT) || IS_TYPE(ty, T_SPEC_DOUBLE));
}


//Is integer
bool is_integer(Decl const* dcl)
{
    ASSERTN(DECL_dt(dcl) == DCL_TYPE_NAME || DECL_dt(dcl) == DCL_DECLARATION,
           ("expect type-name or dcrlaration"));
    return is_integer(DECL_spec(dcl));
}


//Is integer
bool is_integer(TypeSpec const* ty)
{
    return (IS_TYPE(ty, T_SPEC_CHAR) ||
            IS_TYPE(ty, T_SPEC_SHORT)||
            IS_TYPE(ty, T_SPEC_INT)  ||
            IS_TYPE(ty, T_SPEC_LONG) ||
            IS_TYPE(ty, T_SPEC_LONGLONG) ||
            IS_TYPE(ty, T_SPEC_SIGNED) ||
            IS_TYPE(ty, T_SPEC_UNSIGNED) ||
            IS_TYPE(ty, T_SPEC_ENUM));
}


//Return true for arithmetic type which include integer and float.
bool is_arith(Decl const* dcl)
{
    ASSERTN(DECL_dt(dcl) == DCL_TYPE_NAME || DECL_dt(dcl) == DCL_DECLARATION,
            ("expect type-name or dcrlaration"));
    TypeSpec * ty = DECL_spec(dcl);
    return is_scalar(dcl) && (is_integer(ty) || is_fp(ty));
}


bool is_any(Decl const* dcl)
{
    return HAVE_FLAG(TYPE_des(DECL_spec(dcl)), T_SPEC_VOID);
}


//Return true if the return-value type is VOID.
bool is_fun_void_return(Decl * dcl)
{
    if (!is_fun_decl(dcl)) { return false; }
    if (HAVE_FLAG(TYPE_des(DECL_spec(dcl)), T_SPEC_VOID) && !is_pointer(dcl)) {
        return true;
    }
    return false;
}


//Return true if 'dcl' is function-type declaration or reference.
bool is_fun_decl(Decl const* dcl)
{
    dcl = get_pure_declarator(dcl);
    while (dcl != nullptr) {
        switch (DECL_dt(dcl)) {
        case DCL_FUN:
            if (DECL_prev(dcl) == nullptr ||
                (DECL_prev(dcl) != nullptr &&
                 DECL_dt(DECL_prev(dcl)) == DCL_ID)) {
                //CASE:
                //  ID->FUN is func-decl,
                //  e.g: void f()
                //
                //  ID->FUN->... is func-decl,
                //  e.g: void ( * f() ) [], ID->FUN->*->[]
                //
                //  ID->*->FUN->... is NOT func-decl, it is a func-pointer
                //  e.g: void (* (* f)() ) [], ID->*->FUN->*->[]
                return true;
            }
            return false;
        case DCL_ID:
        case DCL_VARIABLE:
            break;
        default:
            ASSERTN(DECL_dt(dcl) != DCL_DECLARATION &&
                    DECL_dt(dcl) != DCL_DECLARATOR &&
                    DECL_dt(dcl) != DCL_ABS_DECLARATOR &&
                    DECL_dt(dcl) != DCL_TYPE_NAME,
                    ("\nunsuitable Decl type locate here in is_fun()\n"));
            return false;
        }
        dcl = DECL_next(dcl);
    }
    return false;
}


//Pointer, array, struct, union are not scalar type.
bool is_scalar(Decl const* dcl)
{
    return get_pure_declarator(dcl) == nullptr;
}


//Return true if 'dcl' is function pointer variable.
bool is_fun_pointer(Decl const* dcl)
{
    dcl = get_pure_declarator(dcl);
    while (dcl != nullptr) {
        switch (DECL_dt(dcl)) {
        case DCL_FUN:
            //CASE:
            //    ID->FUN is func-decl,
            //    e.g: void f()
            //
            //    ID->FUN->... is func-decl
            //    e.g: void ( * f() ) [], ID->FUN->*->[]
            //
            //    ID->*->FUN->... is func-pointer
            //    e.g: void (* (* f)() ) [], ID->*->FUN->*->[]
            if (DECL_prev(dcl) != nullptr &&
                DECL_dt(DECL_prev(dcl)) == DCL_POINTER) {
                return true;
            }
            return false;
        case DCL_ID:
        case DCL_VARIABLE:
            break;
        default:
            ASSERTN(DECL_dt(dcl) != DCL_DECLARATION &&
                   DECL_dt(dcl) != DCL_DECLARATOR &&
                   DECL_dt(dcl) != DCL_ABS_DECLARATOR &&
                   DECL_dt(dcl) != DCL_TYPE_NAME,
                   ("\nunsuitable Decl type locate here in is_fun()\n"));
            return false;
        }
        dcl = DECL_next(dcl);
    }
    return false;
}


bool is_pointer_point_to_array(Decl const* decl)
{
    if (!is_pointer(decl)) { return false; }
    Decl const* base_decl = get_pointer_base_decl(decl, nullptr);
    return base_decl != nullptr && is_array(base_decl);
}


//Is 'dcl' a pointer-declarator,
//e.g:Given Decl as : 'int * a', then the second decltor in the type-chain
//    must be DCL_POINTER, the first is DCL_ID 'a'.
//    And simplar for abs-decl, as an example 'int *', the first decltor
//    in the type-chain must be DCL_POINTER.
bool is_pointer(Decl const* dcl)
{
    dcl = get_pure_declarator(dcl);
    while (dcl != nullptr) {
        switch (DECL_dt(dcl)) {
        case DCL_FUN:
            //function-pointer type:
            //    DCL_POINTER->DCL_FUN
            //function-decl type:
            //    DCL_ID->DCL_FUN
            if (DECL_prev(dcl) != nullptr &&
                DECL_dt(DECL_prev(dcl)) == DCL_POINTER) {
                return true;
            }
            return false;
        case DCL_POINTER:
            return true;
        case DCL_ID:
        case DCL_VARIABLE:
            break;
        default:
            ASSERTN(DECL_dt(dcl) != DCL_DECLARATION &&
                    DECL_dt(dcl) != DCL_DECLARATOR &&
                    DECL_dt(dcl) != DCL_ABS_DECLARATOR &&
                    DECL_dt(dcl) != DCL_TYPE_NAME,
                    ("\nunsuitable Decl type locate here in is_pointer()\n"));
            return false;
        }
        dcl = DECL_next(dcl);
    }
    return false;
}


//The function get the base decloarator of ARRAY in decl-list.
//e.g: the function will return 'pointer' decl-type.
//    declaration----
//        |          |--type-spec (int)
//        |          |--declarator1 (DCL_DECLARATOR)
//        |                |---decl-type (id:a)
//        |                |---decl-type (array) the highest dimension
//        |                |---decl-type (array)
//        |                |---decl-type (pointer)
Decl const* get_array_base_declarator(Decl const* dcl)
{
    dcl = get_pure_declarator(dcl);
    while (dcl != nullptr) {
        switch (DECL_dt(dcl)) {
        case DCL_ARRAY:
            while (dcl != nullptr  && DECL_dt(dcl) == DCL_ARRAY) {
                dcl = DECL_next(dcl);
            }
            return dcl;
        case DCL_ID:
        case DCL_VARIABLE:
            break;
        default:
            ASSERTN(DECL_dt(dcl) != DCL_DECLARATION &&
                    DECL_dt(dcl) != DCL_DECLARATOR &&
                    DECL_dt(dcl) != DCL_ABS_DECLARATOR &&
                    DECL_dt(dcl) != DCL_TYPE_NAME,
                    ("\nunsuitable Decl type locate here in is_array()\n"));
            return nullptr;
        }
        dcl = DECL_next(dcl);
    }
    return nullptr;
}


//The function get the POINTER decl-type in decl-list.
//   declaration----
//       |          |--type-spec (int)
//       |          |--declarator1 (DCL_DECLARATOR)
//       |                |---decl-type (id:a)
//       |                |---decl-type (pointer)
//
//e.g:given Decl as : 'int * a', then the second decl-type in the decl-list
//    must be DCL_POINTER, the first is DCL_ID 'a'.
//    And simplar for abs-decl, as an example 'int *', the first decltor
//    in the type-chain must be DCL_POINTER.
Decl const* get_pointer_declarator(Decl const* dcl)
{
    dcl = get_pure_declarator(dcl);
    while (dcl != nullptr) {
        switch (DECL_dt(dcl)) {
        case DCL_FUN:
            //function-pointer type:
            //  DCL_POINTER->DCL_FUN
            //function-decl type:
            //  DCL_ID->DCL_FUN
            if (DECL_prev(dcl) != nullptr &&
                DECL_dt(DECL_prev(dcl)) == DCL_POINTER) {
                return dcl;
            }
            return nullptr;
        case DCL_POINTER:
            return dcl;
        case DCL_ID:
        case DCL_VARIABLE:
            break;
        default:
            ASSERTN(DECL_dt(dcl) != DCL_DECLARATION &&
                    DECL_dt(dcl) != DCL_DECLARATOR &&
                    DECL_dt(dcl) != DCL_ABS_DECLARATOR &&
                    DECL_dt(dcl) != DCL_TYPE_NAME,
                    ("\nunsuitable Decl type locate here in is_pointer()\n"));
            return nullptr;
        }
        dcl = DECL_next(dcl);
    }
    return nullptr;
}


//Is dcl is array declarator,
//    for decl : int a[], the second decltor must be DCL_ARRAY,
//    the first is DCL_ID.
//    for abs-decl: int [], the first decltor must be DCL_ARRAY.
bool is_array(Decl const* dcl)
{
    dcl = get_pure_declarator(dcl);
    while (dcl != nullptr) {
        switch (DECL_dt(dcl)) {
        case DCL_ARRAY:
            return true;
        case DCL_ID:
        case DCL_VARIABLE:
            break;
        default:
            ASSERTN(DECL_dt(dcl) != DCL_DECLARATION &&
                   DECL_dt(dcl) != DCL_DECLARATOR &&
                   DECL_dt(dcl) != DCL_ABS_DECLARATOR &&
                   DECL_dt(dcl) != DCL_TYPE_NAME,
                   ("\nunsuitable Decl type locate here in is_array()\n"));
            return false;
        }
        dcl = DECL_next(dcl);
    }
    return false;
}


//Create a new type-name, and ONLY copy declaration info list and type-spec.
Decl * cp_type_name(Decl const* src)
{
    ASSERTN(DECL_dt(src) == DCL_TYPE_NAME, ("cp_type_name"));
    Decl * dest = new_decl(DCL_TYPE_NAME);
    DECL_decl_list(dest) = cp_decl(DECL_decl_list(src));
    PURE_DECL(dest) = nullptr;
    DECL_spec(dest) = DECL_spec(src);

    Decl * p = PURE_DECL(src), * q;
    while (p != nullptr) {
        q = cp_decl(p);
        xcom::add_next(&PURE_DECL(dest), q);
        p = DECL_next(p);
    }
    return dest;
}


Struct * get_struct_spec(Decl const* decl)
{
    ASSERT0(is_struct(decl));
    return TYPE_struct_type(DECL_spec(decl));

}


Union * get_union_spec(Decl const* decl)
{
    ASSERT0(is_union(decl));
    return TYPE_union_type(DECL_spec(decl));
}


Aggr * get_aggr_spec(Decl const* decl)
{
    ASSERT0(is_struct(decl) || is_union(decl));
    return is_struct(decl) ? (Aggr*)get_struct_spec(decl) :
                             (Aggr*)get_union_spec(decl);
}


//Get offset of appointed 'name' in struct/union 'st'.
UINT get_aggr_field(Aggr const* s, CHAR const* name, Decl ** fld_decl)
{
    Decl * dcl = AGGR_decl_list(s);
    UINT ofst = 0;
    while (dcl != nullptr) {
        Sym * sym = get_decl_sym(dcl);
        if (::strcmp(name, SYM_name(sym)) == 0) {
            if (fld_decl != nullptr) {
                *fld_decl = dcl;
            }
            return ofst;
        }
        UINT elem_bytesize = 0;
        ofst = compute_field_ofst(s, ofst, dcl, AGGR_field_align(s),
                                  &elem_bytesize);
        dcl = DECL_next(dcl);
    }
    ASSERTN(0, ("Unknown aggregate field"));
    return 0;
}


TypeSpec const* get_decl_spec(Decl const* decl)
{
    return DECL_spec(decl);
}


//Get offset and declaration of field indexed by 'idx'.
//idx: the idx of field, start at 0.
UINT get_aggr_field(Aggr const* s, INT idx, Decl ** fld_decl)
{
    Decl * dcl = AGGR_decl_list(s);
    UINT ofst = 0;
    UINT size = 0;
    while (dcl != nullptr && idx >= 0) {
        Sym * sym = get_decl_sym(dcl);
        if (idx == 0) {
            if (fld_decl != nullptr) {
                *fld_decl = dcl;
            }
            return ofst;
        }
        UINT elem_bytesize = 0;
        ofst = compute_field_ofst(s, ofst, dcl, AGGR_field_align(s),
                                  &elem_bytesize);
        dcl = DECL_next(dcl);
        idx--;
    }
    ASSERTN(0, ("Unknown aggregate field"));
    return 0;
}


static void remove_redundant_para(Decl * declaration)
{
    ASSERT0(DECL_dt(declaration) == DCL_DECLARATION ||
        DECL_dt(declaration) == DCL_TYPE_NAME);
    Decl * dclor;
    Decl * para_list = get_parameter_list(declaration, &dclor);
    if (para_list != nullptr) {
        TypeSpec * spec = DECL_spec(para_list);
        ASSERT0(spec != nullptr);
        if (IS_TYPE(spec, T_SPEC_VOID)) {
            if (is_abs_declaraotr(para_list) && !is_pointer(para_list)) {
                //e.g int foo(void), there is no any parameter.
                DECL_fun_para_list(dclor) = nullptr;
                return;
            }
            if (!is_abs_declaraotr(para_list) && !is_pointer(para_list)) {
                err(g_real_line_num, "the first parameter has incomplete type");
                return;
            }
        }
    }
}


//Check struct/union completeness if decl is struct/union.
static bool check_struct_union_complete(Decl * decl)
{
    TypeSpec * type_spec = DECL_spec(decl);
    if (is_struct(type_spec) || is_union(type_spec)) {
        Sym * sym = get_decl_sym(decl);
        CHAR const* name = SYM_name(sym);
        if (!is_pointer(decl)) {
            bool e = false; //claim error
            CHAR const* t = nullptr;
            if (is_struct(type_spec) && !is_struct_complete(type_spec)) {
                e = true;
                t = "struct";
            } else if (is_union(type_spec) && !is_union_complete(type_spec)) {
                e = false;
                t = "union";
            }
            if (e) {
                //error occur!
                ASSERT0(t);
                StrBuf buf(64);
                if (name != nullptr) {
                    format_struct_union_complete(buf,
                        get_pure_type_spec(type_spec));
                    err(g_real_line_num,
                        "'%s' uses incomplete defined %s : %s",
                        name, t, buf.buf);
                    return false;
                } else {
                    err(g_real_line_num,
                        "uses incomplete definfed %s without name", t);
                    return false;
                }
            }
        }
    }
    return true;
}


static bool check_bitfield(Decl * decl)
{
    if (is_bitfield(decl) && is_pointer(decl)) {
        err(g_real_line_num, "pointer type can not assign bit length");
        return false;
    }
    return true;
}


static bool func_def(Decl * declaration)
{
    //Function definition only permit in global scope in C spec.
    if (SCOPE_level(g_cur_scope) != GLOBAL_SCOPE) {
        err(g_real_line_num,
            "miss ';' before '{' , function define should at global scope");
        return false;
    }

    //Check if 'decl' is unique at scope declaration list.
    Decl * dcl = SCOPE_decl_list(g_cur_scope);
    while (dcl != nullptr) {
        if (is_decl_equal(dcl, declaration) && declaration != dcl
            && DECL_is_fun_def(dcl)) {
            err(g_real_line_num, "function '%s' already defined",
                SYM_name(get_decl_sym(dcl)));
            return false;
        }
        dcl = DECL_next(dcl);
    }

    //Add decl to scope here to support recursive func-call.
    xcom::add_next(&SCOPE_decl_list(g_cur_scope), declaration);

    //At function definition mode, identifier of each
    //parameters cannot be nullptr.
    if (is_abs_declaraotr(DECL_decl_list(declaration))) {
        err(g_real_line_num,
            "expected formal parameter list, not a type list");
        return false;
    }

    remove_redundant_para(declaration);
    Decl * para_list = get_parameter_list(declaration);
    DECL_fun_body(declaration) = compound_stmt(para_list);
    //dump_scope(DECL_fun_body(declaration), 0xfffFFFF);

    DECL_is_fun_def(declaration) = true;
    ASSERTN(SCOPE_level(g_cur_scope) == GLOBAL_SCOPE,
            ("Funtion declaration should in global scope"));

    refine_func(declaration);
    if (ST_SUCC != label_ck(get_last_sub_scope(g_cur_scope))) {
        err(g_real_line_num, "illegal label used");
        return false;
    }

    //Check return value at typeck.cpp if
    //'DECL_fun_body(dcl)' is nullptr
    return true;
}


static Decl * factor_user_type_rec(Decl * decl, TypeSpec ** new_spec)
{
    ASSERT0(DECL_dt(decl) == DCL_DECLARATION || DECL_dt(decl) == DCL_TYPE_NAME);
    TypeSpec * spec = DECL_spec(decl);
    Decl * new_declor = nullptr;
    if (IS_USER_TYPE_REF(spec)) {
        new_declor = factor_user_type_rec(TYPE_user_type(spec), new_spec);
    } else {
        *new_spec = cp_spec(spec);
        REMOVE_FLAG(TYPE_des(*new_spec), T_STOR_TYPEDEF);
    }

    Decl * cur_declor = const_cast<Decl*>(get_pure_declarator(decl));
    if (DECL_dt(cur_declor) == DCL_ID) {
        //neglect the first DCL_ID node, we only need the rest.
        cur_declor = DECL_next(cur_declor);
    }

    cur_declor = cp_decl_begin_at(cur_declor);
    xcom::insertbefore(&new_declor, new_declor, cur_declor);
    return new_declor;
}


//Expanding user-defined type, declared with 'typedef' in C
Decl * expand_user_type(Decl * ut)
{
    ASSERT0(is_user_type_ref(ut) || is_user_type_decl(ut));
    ASSERT0(DECL_dt(ut) == DCL_TYPE_NAME || DECL_dt(ut) == DCL_DECLARATION);
    if (is_user_type_ref(ut)) {
        Decl * tmp = expand_user_type(TYPE_user_type(DECL_spec(ut)));
        ASSERT0(DECL_spec(tmp) != nullptr);
        REMOVE_FLAG(TYPE_des(DECL_spec(tmp)), T_STOR_TYPEDEF);
        DECL_spec(ut) = DECL_spec(tmp);
        tmp = const_cast<Decl*>(get_pure_declarator(tmp));
        if (DECL_dt(tmp) == DCL_ID) {
            tmp = DECL_next(tmp);
        }
        if (tmp != nullptr) {
            xcom::add_next(&PURE_DECL(ut), tmp);
        }
        return ut;
    }

    Decl * tmp = cp_decl_fully(ut);
    ASSERT0(DECL_spec(tmp) != nullptr);
    REMOVE_FLAG(TYPE_des(DECL_spec(tmp)), T_STOR_TYPEDEF);
    return tmp;
}


//Factor the compound user type into basic type.
//e.g: typedef int * INTP;
//    'INTP a' will be factored to 'int * a'.
Decl * factor_user_type(Decl * decl)
{
    ASSERT0(DECL_dt(decl) == DCL_DECLARATION ||
            DECL_dt(decl) == DCL_TYPE_NAME);
    TypeSpec * spec = DECL_spec(decl);
    ASSERT0(IS_USER_TYPE_REF(spec));

    //Indicate current specifier is typedef operation.
    bool is_typedef = IS_TYPEDEF(spec);

    //Create new type specifer according to the factored type information.
    TypeSpec * new_spec = nullptr;
    Decl * new_declor = factor_user_type_rec(TYPE_user_type(spec), &new_spec);
    ASSERT0(new_spec);
    if (is_typedef) {
        SET_FLAG(TYPE_des(new_spec), T_STOR_TYPEDEF);
    }
    SET_FLAG(TYPE_des(new_spec), (TYPE_des(spec) & T_STOR_REG));
    SET_FLAG(TYPE_des(new_spec), (TYPE_des(spec) & T_STOR_EXTERN));
    SET_FLAG(TYPE_des(new_spec), (TYPE_des(spec) & T_STOR_INLINE));
    SET_FLAG(TYPE_des(new_spec), (TYPE_des(spec) & T_STOR_STATIC));
    SET_FLAG(TYPE_des(new_spec), (TYPE_des(spec) & T_STOR_AUTO));

    Tree * inittree = nullptr;
    if (DECL_dt(decl) == DCL_DECLARATION && is_initialized(decl)) {
        inittree = get_decl_init_tree(decl);
    }
    Decl * cur_declor = const_cast<Decl*>(get_pure_declarator(decl));
    if (cur_declor == nullptr) {
        //cur_declor may be abstract declarator list.
        //There is at least DCL_ID if the declaration is typedef.
        ASSERT0(!is_typedef);
        return new_declaration(new_spec, new_declor, g_cur_scope, inittree);
    }

    ASSERTN(DECL_decl_list(decl), ("miss declarator"));
    ASSERTN(DECL_dt(cur_declor) == DCL_ID ||
            DECL_dt(DECL_decl_list(decl)) == DCL_ABS_DECLARATOR,
        ("either decl is abstract declarator or miss typedef/variable name."));

    //Neglect the first DCL_ID node, we only need the rest.
    xcom::insertbefore(&new_declor, new_declor,
                       cp_decl_begin_at(DECL_next(cur_declor)));

    //Put an ID to be the head of declarator list.
    DECL_next(cur_declor) = nullptr;
    ASSERT0(DECL_prev(cur_declor) == nullptr);
    xcom::insertbefore_one(&new_declor, new_declor, cur_declor);

    //Create a new declaration with factored specifier.
    return new_declaration(new_spec, new_declor, g_cur_scope, inittree);
}


static void inferEnumValue(EnumValueList * evals)
{
    INT i = 0;
    for (; evals != nullptr; evals = EVAL_LIST_next(evals), i++) {
        if (EVAL_LIST_val(evals) != 0) {
            i = EVAL_LIST_val(evals);
            continue;
        }

        EVAL_LIST_val(evals) = i;
    }
}


static void process_enum(TypeSpec * ty)
{
    if (!IS_ENUM_TYPE(ty) || TYPE_enum_type(ty) == nullptr) { return; }

    EnumValueList * evals = ENUM_vallist(TYPE_enum_type(ty));
    if (evals == nullptr) { return; }

    inferEnumValue(evals);

    EnumList * elst = (EnumList*)xmalloc(sizeof(EnumList));
    ENUM_LIST_enum(elst) = TYPE_enum_type(ty);

    ASSERT0(!find_enum(SCOPE_enum_list(g_cur_scope), TYPE_enum_type(ty)));
    xcom::insertbefore_one(&SCOPE_enum_list(g_cur_scope),
                           SCOPE_enum_list(g_cur_scope), elst);
}


void fix_extern_array_size(Decl * declaration)
{
    ASSERT0(DECL_dt(declaration) == DCL_DECLARATION);
    ASSERT0(is_array(declaration));
    if (!is_extern(declaration)) {
        return;
    }
    Decl * decl = const_cast<Decl*>(get_pure_declarator(declaration));
    if (DECL_dt(decl) == DCL_ID) {
        decl = DECL_next(decl);
    }
    if (decl == nullptr) {
        return;
    }
    while (decl != nullptr && DECL_dt(decl) == DCL_ARRAY) {
        UINT dimsz = (UINT)DECL_array_dim(decl);
        if (dimsz == 0) {
            DECL_array_dim(decl) = 1;
        }
        decl = DECL_next(decl);
    }
}


//Postprocess init declaration list.
//Split them into a list of declarations via generating the DCL_DECLARATION
//accroding TypeSpec, DECLLARATOR.
bool post_init_declarator_list(Decl * dcl_list, TypeSpec * type_spec,
                               UINT lineno, bool * is_last_fun_def)
{
    *is_last_fun_def = false;
    while (dcl_list != nullptr) {
        Decl * dcl = dcl_list;
        dcl_list = DECL_next(dcl_list);

        //Generate the DCL_DECLARATION that composed by TypeSpec
        //and DCL_DECLARATOR for each chained Declarators.
        DECL_next(dcl) = DECL_prev(dcl) = nullptr;

        Decl * declaration = new_decl(DCL_DECLARATION);
        DECL_spec(declaration) = type_spec;
        DECL_decl_list(declaration) = dcl;
        DECL_align(declaration) = g_alignment;
        DECL_decl_scope(declaration) = g_cur_scope;
        DECL_lineno(declaration) = lineno;

        if (IS_USER_TYPE_REF(type_spec)) {
            declaration = factor_user_type(declaration);
            DECL_align(declaration) = g_alignment;
            DECL_decl_scope(declaration) = g_cur_scope;
            DECL_lineno(declaration) = lineno;
        }

        if (is_fun_decl(declaration)) {
            if (g_real_token == T_LLPAREN) {
                if (!func_def(declaration)) {
                    return false;
                }
            } else if (g_real_token == T_SEMI) {
                //Function Declaration.
                xcom::add_next(&SCOPE_decl_list(g_cur_scope), declaration);
                DECL_is_fun_def(declaration) = 0;
            } else {
                err(g_real_line_num,
                    "illegal function definition/declaration, "
                    "might be miss ';'");
                return false;
            }
        } else {
            //Common variable definition/declaration.
            //Check the declarator that should be unique at current scope.
            if (!is_unique_decl(SCOPE_decl_list(g_cur_scope), declaration)) {
                err(g_real_line_num, "'%s' already defined",
                    SYM_name(get_decl_sym(declaration)));
                return false;
            }
            xcom::add_next(&SCOPE_decl_list(g_cur_scope), declaration);
        }

        if (is_user_type_decl(declaration)) { //typedef declaration
            //As the preivous parsing in 'declarator()' has recoginzed that
            //current identifier is identical exactly in current scope,
            //it is dispensable to warry about the redefinition, even if
            //invoking is_user_type_exist().
            addToUserTypeList(&SCOPE_user_type_list(g_cur_scope), declaration);
        }

        if (!check_struct_union_complete(declaration)) {
            return false;
        }

        if (!check_bitfield(declaration)) {
            return false;
        }

        if (is_initialized(declaration)) {
            process_init(declaration);
        } else {
            //Check the size of array dimension.
            if (is_array(declaration)) {
                fix_extern_array_size(declaration);

                //This function also do check in addition to compute array size.
                get_decl_size(declaration);
            }
        }

        *is_last_fun_def = DECL_is_fun_def(declaration);
    }
    return true;
}


//declaration:
//  declaration_spec init_declarator_list;
//  declaration_spec ;
//Return true if variable declaration is found.
bool declaration()
{
    UINT lineno = g_real_line_num;
    TypeSpec * type_spec = declaration_spec();
    if (type_spec == nullptr) { return false; }

    TypeSpec * qualifier = new_type();
    extract_qualifier(type_spec, qualifier);
    complement_qua(type_spec);

    process_enum(type_spec);

    Decl * dcl_list = init_declarator_list(qualifier);
    if (dcl_list == nullptr) {
        //For enum type, there is no enum variable declared, such as:
        //    enum {X, Y, Z};
        //    enum _tag {X, Y, Z};
        return false; //no variable declared
    }

    bool def_or_init_var = true;
    DECL_align(dcl_list) = g_alignment;
    if (DECL_child(dcl_list) == nullptr) {
        err(g_real_line_num, "declaration expected identifier");
        return def_or_init_var;
    }
    
    bool is_last_fun_def = false;
    if (!post_init_declarator_list(dcl_list, type_spec,
                                   lineno, &is_last_fun_def)) {
        return def_or_init_var;
    }

    if (!is_last_fun_def) {
        if (g_real_token != T_SEMI) {
            err(g_real_line_num, "expected ';' after declaration");
        } else {
            match(T_SEMI);
        }
    }

    return def_or_init_var;
}


bool declaration_list()
{
    bool find = false;
    while (is_in_first_set_of_declarator()) {
        find |= declaration();
    }
    return find;
}
