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

#define BUILD_TYNAME(T)  buildTypeName(buildBaseTypeSpec(T))

static TypeSpec * g_schar_type;
static TypeSpec * g_sshort_type;
static TypeSpec * g_sint_type;
static TypeSpec * g_slong_type;
static TypeSpec * g_slonglong_type;
static TypeSpec * g_uchar_type;
static TypeSpec * g_ushort_type;
static TypeSpec * g_uint_type;
static TypeSpec * g_ulong_type;
static TypeSpec * g_ulonglong_type;
static TypeSpec * g_float_type;
static TypeSpec * g_double_type;
static TypeSpec * g_void_type;
static TypeSpec * g_enum_type;

static INT process_pointer_init(Decl * dcl, TypeSpec * ty, Tree ** init);
static INT process_struct_init(TypeSpec * ty, Tree ** init);
static INT process_union_init(TypeSpec * ty, Tree ** init);
static INT process_base_init(TypeSpec * ty, Tree ** init);
static TypeSpec * buildBaseTypeSpec(INT des);
static INT TypeTran(Tree * t, TYCtx * cont);

//Go through the init tree , 'dcl' must be DCL_ARRAY
//NOTE: compute_array_dim() should be invoked.
//      dcl might be changed.
static INT process_array_init_recur(Decl * dcl, TypeSpec * ty, Tree ** init)
{
    if (DECL_dt(dcl) == DCL_ID) {
        dcl = DECL_next(dcl);
    }
    ASSERTN(DECL_dt(dcl) == DCL_ARRAY, ("ONLY can be DCL_ARRAY"));
    ULONGLONG dim = DECL_array_dim(dcl);
    ULONGLONG count = 0;

    Decl * head = dcl;
    Decl * tail = nullptr;
    while (DECL_next(dcl) != nullptr && DECL_dt(DECL_next(dcl)) == DCL_ARRAY) {
        dcl = DECL_next(dcl);
    }
    tail = dcl;

    INT st = ST_SUCC;
    if (head != tail) {
        //multipul dimension array.
        while (*init != nullptr && st == ST_SUCC) {
            st = process_array_init_recur(DECL_next(head), ty, init);
            count++;
            if (dim > 0 && count >= dim) {
                break;
            }
        }
    } else {
        //Single dimension array.
        if (TREE_type(*init) == TR_INITVAL_SCOPE) {
            //When we meet TR_INITVAL_SCOPE, because we are
            //initializing an array, thus the initial value set begin at
            //subset of TR_INITVAL_SCOPE.
            Tree * t = TREE_initval_scope(*init);
            ty = get_pure_type_spec(ty);
            while (t != nullptr && st == ST_SUCC) {
                if (is_struct(ty)) {
                    st = process_struct_init(ty, &t);
                } else if (is_union(ty)) {
                    st = process_union_init(ty, &t);
                } else if (is_pointer(dcl)) {
                    st = process_pointer_init(const_cast<Decl*>(dcl), ty, &t);
                } else {
                    //simple type init. e.g INT/SHORT/CHAR
                    st = process_base_init(ty, &t);
                }
                count++;
                if (dim > 0 && count >= dim) {
                    break;
                }
            }
            *init = TREE_nsib(*init);
        } else {
            ty = get_pure_type_spec(ty);
            while (*init != nullptr && st == ST_SUCC) {
                if (is_struct(ty)) {
                    st = process_struct_init(ty, init);
                } else if (is_union(ty)) {
                    st = process_union_init(ty, init);
                } else if (is_pointer(dcl)) {
                    st = process_pointer_init(const_cast<Decl*>(dcl), ty, init);
                } else {
                    //simple type init. e.g INT/SHORT/CHAR
                    st = process_base_init(ty, init);
                }
                count++;
                if (dim > 0 && count >= dim) {
                    break;
                }
            }
        }
    } //end else

    if (dim == 0) {
        DECL_array_dim(head) = count;
    }
    return st;
}


//Go through the init tree , 'decl' must be DCL_DECLARATION
//NOTE: compute_array_dim() should be invoked.
static INT process_array_init(Decl * decl, Tree ** init)
{
    ASSERT0(is_array(decl));
    Decl * dcl = const_cast<Decl*>(get_pure_declarator(decl));
    TypeSpec * ty = DECL_spec(decl); //get TypeSpecifier
    ASSERTN(dcl && ty,
            ("DCLARATION must have a DECRLARATOR node and TypeSpec node"));
    return process_array_init_recur(dcl, ty, init);
}


static INT process_pointer_init(Decl * dcl, TypeSpec * ty, Tree ** init)
{
    Tree * t = TREE_initval_scope(*init);
    if (TREE_type(*init) == TR_INITVAL_SCOPE) {
        process_pointer_init(dcl, ty, &t);
        *init = TREE_nsib(*init);
        return ST_SUCC;
    }

    //TODO: type check
    //...

    *init = TREE_nsib(*init);
    return ST_SUCC;
}


static INT process_struct_init(TypeSpec * ty, Tree ** init)
{
    ASSERTN(IS_STRUCT(ty), ("ONLY must be struct type-spec"));
    Struct * s = TYPE_struct_type(ty);
    if (!STRUCT_is_complete(s)) {
        err(g_real_line_num, "uses incomplete struct %s",
            SYM_name(STRUCT_tag(s)));
        return ST_ERR;
    }

    if (*init == nullptr) {
        //There is no initial value, nothing to check.
        return ST_SUCC;
    }

    if (TREE_type(*init) != TR_INITVAL_SCOPE) {
        err(g_real_line_num, "unmatch initial value type to struct %s",
            STRUCT_tag(s)->getStr());
    }

    //Strip off the initval-scope.
    Tree * fldval = TREE_initval_scope(*init);
    for (Decl * flddcl = STRUCT_decl_list(s);
         flddcl != nullptr && fldval != nullptr; flddcl = DECL_next(flddcl)) {
        INT st = ST_SUCC;
        if ((st = process_init_by_extra_val(flddcl, &fldval)) != ST_SUCC) {
            return st;
        }
    }
    *init = TREE_nsib(*init);
    return ST_SUCC;
}


static INT process_union_init(TypeSpec * ty, Tree ** init)
{
    ASSERTN(IS_UNION(ty), ("ONLY must be union type-spec"));
    Union * s = TYPE_union_type(ty);
    if (!UNION_is_complete(s)) {
        err(g_real_line_num, "uses incomplete union %s",
            SYM_name(UNION_tag(s)));
        return ST_ERR;
    }

    if (*init == nullptr) {
        //There is no initial value, nothing to check.
        return ST_SUCC;
    }

    if (TREE_type(*init) != TR_INITVAL_SCOPE) {
        err(g_real_line_num, "unmatch initial value type to union %s",
            UNION_tag(s)->getStr());
    }

    //Strip off the initval-scope.
    Tree * fldval = TREE_initval_scope(*init);
    for (Decl * flddcl = UNION_decl_list(s);
         flddcl != nullptr && fldval != nullptr; flddcl = DECL_next(flddcl)) {
        INT st = ST_SUCC;
        if ((st = process_init_by_extra_val(flddcl, &fldval)) != ST_SUCC) {
            return st;
        }
    }
    *init = TREE_nsib(*init);
    return ST_SUCC;
}


//C base type
static INT process_base_init(TypeSpec * ty, Tree ** init)
{
    Tree * t = TREE_initval_scope(*init);
    if (TREE_type(*init) == TR_INITVAL_SCOPE) {
        process_base_init(ty, &t);
        *init = TREE_nsib(*init);
        return ST_SUCC;
    }
    //TODO: type check
    *init = TREE_nsib(*init);
    return ST_SUCC;
}


//Initializing check.
//The function:
//  1. computes the exactly array index for lowest dimension.
//  2. checks compatibility between the type of initial-value and
//     type-specifer.
//decl can ONLY be DCL_DECLARATION.
INT process_init(Decl * decl)
{
    if (decl == nullptr) { return ST_SUCC; }
    ASSERTN(DECL_dt(decl) == DCL_DECLARATION, ("ONLY can be DCRLARATION"));
    if (!is_initialized(decl)) { return ST_SUCC; }

    Decl * dcl = DECL_decl_list(decl); //get DCL_DECLARATOR
    TypeSpec * ty = DECL_spec(decl); //get TypeSpecifier
    ASSERTN(dcl && ty,
            ("DCLARATION must have a DECRLARATOR node and TypeSpec node"));
    ASSERTN(DECL_dt(dcl) == DCL_DECLARATOR, ("ONLY can be DECLARATOR"));
    ASSERT0(DECL_is_init(dcl));
    ASSERT0(DECL_init_tree(dcl));

    Tree * initval = get_decl_init_tree(decl);
    if (initval == nullptr) {
        err(g_real_line_num, "initializing expression is illegal");
        return ST_ERR;
    }

    INT st = ST_SUCC;
    if (is_pointer(dcl)) {
        //First, determine whether 'dcl' is aggregate or array type.
        //e.g: basic type, pointer type.
        st = process_pointer_init(DECL_child(dcl), ty, &initval);
    } else if (is_array(dcl)) {
        st = process_array_init(decl, &initval);
    } else if (is_struct(ty)) {
        st = process_struct_init(ty, &initval);
    } else if (is_union(ty)) {
        st = process_union_init(ty, &initval);
    } else {
        //simple type init. e.g INT SHORT
        st = process_base_init(ty, &initval);
    }

    if (initval != nullptr) {
        ASSERT0(get_decl_sym(decl));
        err(g_real_line_num,
            "there are too many initializers than var '%s' declared",
            SYM_name(get_decl_sym(decl)));
        st = ST_ERR;
    }
    return st;
}


//'decl' does not have its own initializing form tree,
//therefore 'init' will be recorded as the initialization tree.
INT process_init_by_extra_val(Decl * decl, Tree ** init)
{
    if (decl == nullptr) { return ST_SUCC; }
    ASSERTN(DECL_dt(decl) == DCL_DECLARATION, ("ONLY can be DCRLARATION"));

    Decl * dcl = DECL_decl_list(decl); //get DCRLARATOR
    TypeSpec * ty = DECL_spec(decl); //get TypeSpecifier
    ASSERTN((dcl && ty),
            ("DCLARATION must have a DCRLARATOR node and TypeSpec node"));
    ASSERTN((*init) != nullptr,
            ("'init' initialization tree cannot be nullptr"));

    INT st = ST_SUCC;
    if (is_array(dcl)) {
        st = process_array_init(decl, init);
    } else if (is_struct(ty)) {
        st = process_struct_init(ty, init);
    } else if (is_union(ty)) {
        st = process_union_init(ty, init);
    } else if (is_pointer(dcl)) {
        st = process_pointer_init(dcl, ty, init);
    } else {
        //simple type init. e.g INT SHORT
        st = process_base_init(ty,init);
    }

    return st;
}


#ifdef _DEBUG_
//Check 'dcl' is DCL_TYPE_NAME.
//type name often be used in type-cast expression, such as
static bool is_valid_type_name(Decl * dcl)
{
    ASSERT0(dcl && DECL_decl_list(dcl));
    if (!(DECL_dt(dcl) == DCL_TYPE_NAME &&
          DECL_dt(DECL_decl_list(dcl)) == DCL_ABS_DECLARATOR)) {
        return false;
    }

    dcl = DECL_child(DECL_decl_list(dcl));

    //type name does not include ID.
    if (dcl == nullptr || DECL_dt(dcl) == DCL_ARRAY ||
        DECL_dt(dcl) == DCL_POINTER || DECL_dt(dcl) == DCL_FUN) {
        return true;
    }
    return false;
}
#endif


//Constructing TypeSpec-NAME declaration
static Decl * buildTypeName(TypeSpec * ty)
{
    ASSERT0(ty);
    Decl * decl = new_decl(DCL_TYPE_NAME);
    DECL_decl_list(decl) = new_decl(DCL_ABS_DECLARATOR);
    DECL_spec(decl) = ty;
    return decl;
}


//Only construct simply base type-spec
static TypeSpec * buildBaseTypeSpec(INT des)
{
    if (!is_simple_base_type(des)) {
        ASSERTN(0,("expect base type"));
        return nullptr;
    }

    if (IS_TYPED(des, T_SPEC_SIGNED)) {
        if (IS_TYPED(des, T_SPEC_CHAR)) {
            return g_schar_type;
        } else if (IS_TYPED(des, T_SPEC_SHORT)) {
            return g_sshort_type;
        } else if (IS_TYPED(des, T_SPEC_INT)) {
            return g_sint_type;
        } else if (IS_TYPED(des, T_SPEC_LONG)) {
            return g_slong_type;
        } else if (IS_TYPED(des, T_SPEC_LONGLONG)) {
            return g_slonglong_type;
        } else if (IS_TYPED(des, T_SPEC_FLOAT)) {
            return g_float_type;
        } else if (IS_TYPED(des, T_SPEC_DOUBLE)) {
            return g_double_type;
        } else if (IS_TYPED(des, T_SPEC_VOID)) {
            return g_sint_type;
        }
    } else if (IS_TYPED(des, T_SPEC_UNSIGNED)) {
        if (IS_TYPED(des, T_SPEC_CHAR)) {
            return g_uchar_type;
        } else if (IS_TYPED(des, T_SPEC_SHORT)) {
            return g_ushort_type;
        } else if (IS_TYPED(des, T_SPEC_INT)) {
            return g_uint_type;
        } else if (IS_TYPED(des, T_SPEC_LONG)) {
            return g_ulong_type;
        } else if (IS_TYPED(des, T_SPEC_LONGLONG)) {
            return g_ulonglong_type;
        } else if (IS_TYPED(des, T_SPEC_FLOAT)) {
            return g_float_type;
        } else if (IS_TYPED(des, T_SPEC_DOUBLE)) {
            return g_double_type;
        } else if (IS_TYPED(des, T_SPEC_VOID)) {
            return g_uint_type;
        }
    } else {
        if (IS_TYPED(des, T_SPEC_CHAR)) {
            return g_schar_type;
        } else if (IS_TYPED(des, T_SPEC_SHORT)) {
            return g_sshort_type;
        } else if (IS_TYPED(des, T_SPEC_INT)) {
            return g_sint_type;
        } else if (IS_TYPED(des, T_SPEC_LONG)) {
            return g_slong_type;
        } else if (IS_TYPED(des, T_SPEC_LONGLONG)) {
            return g_slonglong_type;
        } else if (IS_TYPED(des, T_SPEC_FLOAT)) {
            return g_float_type;
        } else if (IS_TYPED(des, T_SPEC_DOUBLE)) {
            return g_double_type;
        } else if (IS_TYPED(des, T_SPEC_VOID)) {
            return g_void_type;
        } else if (IS_TYPED(des, T_SPEC_ENUM)) {
            return g_enum_type;
        }
    }
    ASSERTN(0,("TODO"));
    return nullptr;
}


//Conversion rank. Accroding C99 binary operation converting rules.
//l                r                       standard C convert
//double           any                     double
//float            any                     float
//unsigned         unsigned                upper rank unsigned
//signed           signed                  upper rank signed
//unsigned         lower rank signed       unsigned
//unsigned         upper rank signed       upper rank signed
//any              any                     no-convert
static UINT getCvtRank(UINT des)
{
    if (HAVE_FLAG(des, T_SPEC_LONGLONG) || HAVE_FLAG(des, T_SPEC_DOUBLE)) {
        if (HAVE_FLAG(des, T_SPEC_DOUBLE)) {
            return 90;
        }

        //long long
        //long long int
        //signed/unsiged long long int
        return 89;
    }

    if (HAVE_FLAG(des, T_SPEC_LONG) ||
        HAVE_FLAG(des, T_SPEC_DOUBLE) ||
        HAVE_FLAG(des, T_SPEC_FLOAT)) {
        if (HAVE_FLAG(des, T_SPEC_DOUBLE) ||
            HAVE_FLAG(des, T_SPEC_FLOAT)) {
            return 88;
        }

        //long
        //long int
        //signed/unsiged long int
        return 87;
    }

    if (HAVE_FLAG(des, T_SPEC_SHORT)) {
        //short
        //short int
        //signed/unsiged short
        //signed/unsiged short int
        return 84;
    }

    if (HAVE_FLAG(des, T_SPEC_INT) ||
        HAVE_FLAG(des, T_SPEC_ENUM) ||
        ONLY_HAVE_FLAG(des, T_SPEC_SIGNED) ||
        ONLY_HAVE_FLAG(des, T_SPEC_UNSIGNED)) {
        //signed/unsigned
        //int
        //signed/unsiged int
        return 85;
    }

    if (HAVE_FLAG(des, T_SPEC_CHAR) || HAVE_FLAG(des, T_SPEC_BOOL)) {
        //char
        //signed/unsiged char
        return 83;
    }

    UNREACHABLE();
    return 0;
}


//Accroding C99 integer/float conversion rank to build binary operation type.
//l                r                       standard C convert
//double           any                     double
//float            any                     float
//unsigned         unsigned                upper rank unsigned
//signed           signed                  upper rank signed
//unsigned         lower rank signed       unsigned
//unsigned         upper rank signed       upper rank signed
//any              any                     no-convert
static Decl * buildBinaryOpType(TREE_TYPE tok, Decl * l, Decl * r)
{
    TypeSpec * lty = DECL_spec(l);
    TypeSpec * rty = DECL_spec(r);
    UINT bankl = getCvtRank(TYPE_des(lty));
    UINT bankr = getCvtRank(TYPE_des(rty));
    if (bankl > bankr || tok == TR_SHIFT) {
        return l;
    }
    if (bankl == bankr) {
        if (IS_TYPE(lty, T_SPEC_UNSIGNED)) {
            return l;
        }
    }
    return r;
}


static Decl * buildPointerType(TypeSpec * ty)
{
    ASSERT0(ty);
    Decl * newdecl = buildTypeName(ty);
    ASSERT0(DECL_decl_list(newdecl) &&
            DECL_dt(DECL_decl_list(newdecl)) == DCL_ABS_DECLARATOR);
    DECL_child(DECL_decl_list(newdecl)) = new_decl(DCL_POINTER);
    return newdecl;
}


//Checking type-convert of modifier
static bool checkAssign(Tree const* t, Decl * ld, Decl *)
{
    ASSERT0(t && ld);
    StrBuf buf(64);
    if (is_array(ld)) {
        format_declaration(buf, ld);
        err(TREE_lineno(t), "illegal '%s', left operand must be l-value",
            buf.buf);
        return false;
    }

    if (IS_CONST(DECL_spec(ld))) {
        format_declaration(buf, ld);
        err(TREE_lineno(t),
            "illegal '%s', l-value specifies const object", buf.buf);
        return false;
    }

    //TODO: we should check struct/union compatibility in 'ld' and 'rd'
    //Here look lchild of '=' as default type
    return true;
}


//Check parameters type and insert CVT if necessary.
static void insertCvtForParams(Tree * t)
{
    ASSERT0(t && TREE_type(t) == TR_CALL);
    Decl * funcdecl = TREE_result_type(TREE_fun_exp(t));

    ASSERTN(DECL_dt(funcdecl) == DCL_TYPE_NAME, ("expect TypeSpec-NAME"));
    ASSERT0(DECL_decl_list(funcdecl));
    ASSERTN(DECL_dt(DECL_decl_list(funcdecl)) == DCL_ABS_DECLARATOR,
            ("expect abs-declarator"));

    Decl const* formalp_decl = get_parameter_list(funcdecl);
    Tree * newparamlist = nullptr;
    Tree * last = nullptr;
    for (Tree * realp = xcom::removehead(&TREE_para_list(t));
         realp != nullptr && formalp_decl != nullptr;
         realp = xcom::removehead(&TREE_para_list(t))) {
        if (DECL_dt(formalp_decl) == DCL_VARIABLE)  {
            xcom::add_next(&newparamlist, &last, realp);
            continue;
        }

        Decl * realp_decl = TREE_result_type(realp);
        ASSERT0(realp_decl);
        if (is_double(realp_decl) && is_float(formalp_decl)) {
            //Insert convertion operation: truncate double to float.
            realp = gen_cvt(formalp_decl, realp);
        }
        xcom::add_next(&newparamlist, &last, realp);
        formalp_decl = DECL_next(formalp_decl);
    }
    TREE_para_list(t) = newparamlist;
}


static bool findAndRefillStructUnionField(Decl const* base,
                                          Sym const* field_name,
                                          OUT Decl ** field_decl,
                                          INT lineno)
{
    TypeSpec * base_spec = DECL_spec(base);
    Aggr * s = TYPE_aggr_type(base_spec);

    //Search for matched field.
    Decl * field_list = AGGR_decl_list(s);
    if (field_list == nullptr) {
        //Declaration of base type may be defined afterward to
        //reference point, find the complete declaration and backfilling.
        Aggr * findone = nullptr;
        if (!AGGR_is_complete(s) &&
            AGGR_tag(s) != nullptr &&
            is_aggr_exist_in_outer_scope(AGGR_scope(s),
               AGGR_tag(s), base_spec, &findone)) {
            //Try to find complete aggr declaration in outer scope.
            ASSERT0(findone && AGGR_is_complete(findone));
            TYPE_aggr_type(base_spec) = findone;
            field_list = AGGR_decl_list(findone);
        }

        if (field_list == nullptr) {
            //Not find field.
            StrBuf buf(64);
            format_struct_union_complete(buf, base_spec);
            err(lineno,
                " '%s' is an empty %s, '%s' is not its field",
                buf.buf, get_aggr_type_name(base_spec),
                SYM_name(field_name));
            return false;
        }
    }

    while (field_list != nullptr) {
        Sym * sym = get_decl_sym(field_list);
        if (sym == field_name) {
            *field_decl = field_list;
            break;
        }
        field_list = DECL_next(field_list);
    }

    if (*field_decl == nullptr) { return false; }

    if (is_aggr(*field_decl) && !is_aggr_complete(DECL_spec(*field_decl))) {
        //Try to find complete aggr declaration for field.
        Aggr * s2 = TYPE_aggr_type(DECL_spec(*field_decl));
        Aggr * findone2 = nullptr;
        if (AGGR_tag(s2) != nullptr &&
            is_aggr_exist_in_outer_scope(AGGR_scope(s2),
                AGGR_tag(s2), DECL_spec(*field_decl), &findone2)) {
            //Update and refill current field's type-specifier.
            TYPE_aggr_type(DECL_spec(*field_decl)) = findone2;
        }
    }    
    return true;
}


//Return true if t is valid field of given struct/union, otherwise false.
static INT TypeTranIDField(Tree * t, Decl ** field_decl, TYCtx * cont)
{
    ASSERT0(t && TREE_type(t) == TR_ID);
    //At present, the ID may be a field of struct/union,
    //and you need to check out if it is the correct field.

    //Get the struct/union type base.
    Decl * base = TREE_result_type(cont->base_tree_node);
    ASSERTN(base, ("should be struct/union type"));

    if (!findAndRefillStructUnionField(base, TREE_id(t), field_decl,
                                       TREE_lineno(t))) {
        StrBuf buf(64);
        format_struct_union_complete(buf, DECL_spec(base));
        err(TREE_lineno(t), " '%s' : is not a member of type '%s'",
            SYM_name(TREE_id(t)), buf.buf);
        return ST_ERR;
    }
    ASSERT0(*field_decl);
    TREE_id_decl(t) = *field_decl;
    return ST_SUCC;
}


static INT TypeTranID(Tree * t, TYCtx * cont)
{
    ASSERT0(t && TREE_type(t) == TR_ID);
    //Construct type-name and expand user type if it was declared.
    Decl * id_decl = nullptr;
    Tree * parent = TREE_parent(t);
    if (parent != nullptr && cont->is_field &&
        (TREE_type(parent) == TR_DMEM || TREE_type(parent) == TR_INDMEM)) {
        if (ST_SUCC != TypeTranIDField(t, &id_decl, cont)) {
            return ST_ERR;
        }
    } else {
        id_decl = TREE_id_decl(t);
    }
    ASSERT0(id_decl);

    if (is_user_type_ref(id_decl)) {
        //ID has been defined with typedef type.
        //we must expand the combined type here.
        id_decl = expand_user_type(id_decl);
    }

    //Construct TYPE_NAME for ID, that would
    //be used to infer type of tree node.
    TREE_result_type(t) = buildTypeName(DECL_spec(id_decl));
    Decl * res_ty = TREE_result_type(t);
    Decl * declarator = DECL_decl_list(id_decl);

    if (DECL_is_bit_field(declarator)) {
        //Set bit info if idenifier is bitfield.
        //Bitfield info stored at declarator list.
        DECL_is_bit_field(DECL_decl_list(res_ty)) = true;
        DECL_bit_len(DECL_decl_list(res_ty)) = DECL_bit_len(declarator);

        //Check bitfield properties.
        if (is_pointer(id_decl)) {
            StrBuf buf(64);
            format_declaration(buf, id_decl);
            err(TREE_lineno(t),
                "'%s' : pointer cannot assign bit length", buf.buf);
            return ST_ERR;
        }

        if (is_array(id_decl)) {
            StrBuf buf(64);
            format_declaration(buf, id_decl);
            err(TREE_lineno(t),
                "'%s' : array type cannot assign bit length", buf.buf);
            return ST_ERR;
        }

        if (!is_integer(id_decl)) {
            StrBuf buf(64);
            format_declaration(buf, id_decl);
            err(TREE_lineno(t), "'%s' : bit field must have integer type",
                buf.buf);
            return ST_ERR;
        }

        //Check bitfield's base type is big enough to hold it.
        INT size = get_decl_size(id_decl) * BIT_PER_BYTE;
        if (size < DECL_bit_len(declarator)) {
            StrBuf buf(64);
            format_declaration(buf, id_decl);
            err(TREE_lineno(t),
                "'%s' : type of bit field too small for number of bits",
                buf.buf);
            return ST_ERR;
        }
    }

    Decl * dcl_list = const_cast<Decl*>(get_pure_declarator(id_decl));
    ASSERTN(DECL_dt(dcl_list) == DCL_ID,
            ("'id' should be declarator-list-head. Illegal declaration"));

    //Neglect the first DCL_ID node, we only need the rest.
    dcl_list = cp_decl_begin_at(DECL_next(dcl_list));

    //Reduce the number of POINTER of function-pointer to 1.
    //e.g: In C, you can define function pointer like:
    //  int (****fun)();
    //We simply it to be 'int (*fun)()'.
    Decl * tmp = dcl_list;
    while (tmp != nullptr) {
        if (DECL_dt(tmp) == DCL_POINTER) {
            tmp = DECL_next(tmp);
        } else {
            if (DECL_dt(tmp) == DCL_FUN) {
                break;
            } else {
                tmp = dcl_list;
                break;
            }
        }
    }

    if (tmp != nullptr &&
        DECL_dt(tmp) == DCL_FUN &&
        DECL_prev(tmp) != nullptr &&
        DECL_dt(DECL_prev(tmp)) == DCL_POINTER) {
        //Update result type of current tree node to be function pointer.
        PURE_DECL(res_ty) = DECL_prev(tmp);
        DECL_prev(DECL_prev(tmp)) = nullptr;
    } else {
        //Update result type if it has been changed.
        PURE_DECL(res_ty) = tmp != nullptr ? tmp : dcl_list;
    }
    return ST_SUCC;
}


//The function handles dereference of pointer.
static INT TypeTranDeref(Tree * t, TYCtx * cont)
{
    ASSERT0(t);
    if (ST_SUCC != TypeTran(TREE_lchild(t), cont)) {
        return ST_ERR;
    }

    Decl * ld = TREE_result_type(TREE_lchild(t));
    if (!is_pointer(ld) && !is_array(ld)) {
        err(TREE_lineno(t), "Illegal dereferencing operation, "
            "indirection operation should operate on pointer type.");
        return ST_ERR;
    }

    Decl * td = cp_type_name(ld);
    ld = PURE_DECL(td);
    ASSERTN(ld, ("lchild must be pointer type"));
    if (DECL_dt(ld) == DCL_POINTER ||
        DECL_dt(ld) == DCL_ARRAY) {
        //In C, base of array only needs address, so the DEREF
        //operator has alias effect. It means ARRAY(LD(p)) for
        //given declaration: int (*p)[].
        //
        //The value is needed if there is not an ARRAY operator,
        //e.g: a = *p, should generate a=LD(LD(p)).
        xcom::remove(&PURE_DECL(td), ld);
    } else if (DECL_dt(ld) == DCL_FUN) {
        //ACCEPT
    } else {
        err(TREE_lineno(t), "illegal indirection");
        return ST_ERR;
    }
    TREE_result_type(t) = td;
    return ST_SUCC; 
}


static INT TypeTranMulti(Tree * t, TYCtx * cont)
{
    ASSERT0(t);
    if (ST_SUCC != TypeTran(TREE_lchild(t), cont)) { return ST_ERR; }
    if (ST_SUCC != TypeTran(TREE_rchild(t), cont)) { return ST_ERR; }

    Decl * ld = TREE_result_type(TREE_lchild(t));
    Decl * rd = TREE_result_type(TREE_rchild(t));
    if (TREE_token(t) == T_ASTERISK || TREE_token(t) == T_DIV) {
        if (is_arith(ld) && is_arith(rd)) {
            //arithmetic operation
            TREE_result_type(t) = buildBinaryOpType(TREE_type(t), ld, rd);
        } else {
            err(TREE_lineno(t), "illegal operation for '%s'",
                getTokenName(TREE_token(TREE_rchild(t))));
            return ST_ERR;
        }
    } else {
        if (is_integer(ld) && is_integer(rd)) {
            //arithmetic operation
            TREE_result_type(t) = buildBinaryOpType(TREE_type(t), ld, rd);
        } else {
            err(TREE_lineno(t), "illegal operation for '%%'");
            return ST_ERR;
        }
    }
    return ST_SUCC; 
}


static INT TypeTranCond(Tree * t, TYCtx * cont)
{
    ASSERT0(t);
    if (ST_SUCC != TypeTran(TREE_det(t), cont)) { return ST_ERR; }
    if (ST_SUCC != TypeTran(TREE_true_part(t), cont)) { return ST_ERR; }
    if (ST_SUCC != TypeTran(TREE_false_part(t), cont)) { return ST_ERR; }

    Decl * td = TREE_result_type(TREE_true_part(t));
    Decl * fd = TREE_result_type(TREE_false_part(t));
    ASSERT0(td && fd);
    if (is_pointer(td) && !is_pointer(fd)) {
        if (!is_imm_int(TREE_false_part(t)) ||
            TREE_imm_val(TREE_false_part(t)) != 0) {
            err(TREE_lineno(t),
                "no conversion from pointer to non-pointer");
            return ST_ERR;
        }
    } else if (!is_pointer(td) && is_pointer(fd)) {
        if (!is_imm_int(TREE_true_part(t)) ||
            TREE_imm_val(TREE_true_part(t)) != 0) {
            err(TREE_lineno(t),
                "no conversion from pointer to non-pointer");
            return ST_ERR;
        }
    } else if (is_array(td) && !is_array(fd)) {
        err(TREE_lineno(t), "no conversion from array to non-array");
        return ST_ERR;
    } else if (!is_array(td) && is_array(fd)) {
        err(TREE_lineno(t), "no conversion from non-array to array");
        return ST_ERR;
    } else if (is_struct(td) && !is_struct(fd)) {
        err(TREE_lineno(t),
            "can not select between struct and non-struct");
        return ST_ERR;
    } else if (is_union(td) && !is_union(fd)) {
        err(TREE_lineno(t),
            "can not select between union and non-union");
        return ST_ERR;
    }

    //Record true-part type as the result type.
    TREE_result_type(t) = td;
    return ST_SUCC; 
}


static INT TypeTranPreAndPostInc(Tree * t, TYCtx * cont)
{
    ASSERT0(t);
    if (ST_SUCC != TypeTran(TREE_inc_exp(t), cont)) { return ST_ERR; }

    Decl * d = TREE_result_type(TREE_inc_exp(t));
    if (!is_arith(d) && !is_pointer(d)) {
        StrBuf buf(64);
        format_declaration(buf, d);
        if (TREE_type(t) == TR_INC) {
            err(TREE_lineno(t),
                "illegal prefixed '++', for type '%s'", buf.buf);
        } else {
            err(TREE_lineno(t),
                "illegal postfix '++', for type '%s'", buf.buf);
        }
    }
    TREE_result_type(t) = d;
    return ST_SUCC; 
}


static INT TypeTranPreAndPostDec(Tree * t, TYCtx * cont)
{
    ASSERT0(t);
    if (ST_SUCC != TypeTran(TREE_dec_exp(t), cont)) { return ST_ERR; }

    Decl * d = TREE_result_type(TREE_dec_exp(t));
    if (!is_arith(d) && !is_pointer(d)) {
        StrBuf buf(64);
        format_declaration(buf, d);
        if (TREE_type(t) == TR_DEC) {
            err(TREE_lineno(t),
                "illegal prefixed '--' for type '%s'", buf.buf);
        } else {
            err(TREE_lineno(t),
                "illegal postfix '--' for type '%s'", buf.buf);
        }
    }
    TREE_result_type(t) = d;
    return ST_SUCC; 
}


static INT TypeTranSizeof(Tree * t, TYCtx * cont)
{
    ASSERT0(t);
    Tree * kid = TREE_sizeof_exp(t);
    if (kid == nullptr) {
        err(TREE_lineno(t), "miss expression after sizeof");
        return ST_ERR;
    }

    if (TREE_type(kid) == TR_TYPE_NAME) {
        Decl * type_name = TREE_type_name(kid);
        if (IS_USER_TYPE_REF(DECL_spec(type_name))) {
            //Expand the combined type here.
            type_name = expand_user_type(type_name);
            ASSERTN(is_valid_type_name(type_name), 
                    ("Illegal expanding user-type"));
            TREE_type_name(kid) = type_name;
        }
    }

    INT size;
    if (TREE_type(kid) == TR_TYPE_NAME) {
        ASSERT0(TREE_type_name(kid));
        size = get_decl_size(TREE_type_name(kid));
    } else {
        if (ST_SUCC != TypeTran(kid, cont)) { 
            return ST_ERR;
        }
        ASSERT0(TREE_result_type(kid));
        size = get_decl_size(TREE_result_type(kid));
    }
    ASSERT0(size != 0);
    TREE_type(t) = TR_IMMU;
    TREE_imm_val(t) = size;
    return TypeTran(t, cont);
}


static INT TypeTranInDMem(Tree * t, TYCtx * cont)
{
    ASSERT0(t);
    if (ST_SUCC != TypeTran(TREE_base_region(t), cont)) {
        return ST_ERR;
    }

    Decl * ld = TREE_result_type(TREE_base_region(t));
    ASSERTN(TREE_type(TREE_field(t)) == TR_ID, ("illegal TR_INDMEM node!!"));
    if (!IS_STRUCT(DECL_spec(ld)) && !IS_UNION(DECL_spec(ld))) {
        err(TREE_lineno(t), "left of '->' must have struct/union type");
        return ST_ERR;
    }

    cont->is_field = true;
    cont->base_tree_node = TREE_base_region(t);
    if (ST_SUCC != TypeTran(TREE_field(t), cont)) {
        return ST_ERR;
    }

    Decl * rd = TREE_result_type(TREE_field(t));
    cont->base_tree_node = nullptr;
    cont->is_field = false;

    if (!is_pointer(ld)) {
        xoc::Sym * sym = get_decl_sym(TREE_id_decl(TREE_field(t)));
        err(TREE_lineno(t),
            "'->%s' : left operand has 'struct' type, use '.'",
            SYM_name(sym));
        return ST_ERR;
    }

    TREE_result_type(t) = buildTypeName(DECL_spec(rd));
    PURE_DECL(TREE_result_type(t)) = cp_decl_begin_at(PURE_DECL(rd));
    return ST_SUCC; 
}


static INT TypeTranDMem(Tree * t, TYCtx * cont)
{
    ASSERT0(t);
    if (ST_SUCC != TypeTran(TREE_base_region(t), cont)) {
        return ST_ERR; 
    }

    Decl * ld = TREE_result_type(TREE_base_region(t));
    ASSERTN(TREE_type(TREE_field(t)) == TR_ID, ("illegal TR_DMEM node!!"));
    if (!IS_STRUCT(DECL_spec(ld)) && !IS_UNION(DECL_spec(ld))) {
        err(TREE_lineno(t),
            "left of field access operation '.' must be struct/union type");
        return ST_ERR; 
    }
 
    cont->is_field = true;
    cont->base_tree_node = TREE_base_region(t);
    if (ST_SUCC != TypeTran(TREE_field(t), cont)) {
        return ST_ERR; 
    }

    Decl * rd = TREE_result_type(TREE_field(t));
    cont->base_tree_node = nullptr;
    cont->is_field = false;
 
    if (is_pointer(ld)) {
        Sym * sym = get_decl_sym(TREE_id_decl(TREE_field(t)));
        err(TREE_lineno(t),
            "'.%s' : left operand points to 'struct' type, should use '->'",
            SYM_name(sym));
        return ST_ERR; 
    }

    TREE_result_type(t) = buildTypeName(DECL_spec(rd));
    PURE_DECL(TREE_result_type(t)) = cp_decl_begin_at(PURE_DECL(rd));
    return ST_SUCC; 
}


static INT TypeTranArray(Tree * t, TYCtx * cont)
{
    ASSERT0(t);
    //Under C specification, user can derefence pointer
    //utilizing array-operator,
    //e.g:
    //    int ** p;
    //    p[i][j] = 10;
    if (ST_SUCC != TypeTran(TREE_array_base(t), cont)) {
        return ST_ERR;
    }
    if (ST_SUCC != TypeTran(TREE_array_indx(t), cont)) {
        return ST_ERR;
    }

    Decl * ld = TREE_result_type(TREE_array_base(t));

    //Return sub-dimension of base if 'ld' is
    //multi-dimensional array.
    Decl * td = cp_type_name(ld);
    if (PURE_DECL(td) == nullptr) {
        err(TREE_lineno(t),
            "The referrence of array is not match with its declaration.");
    } else if (DECL_dt(PURE_DECL(td)) == DCL_ARRAY ||
               DECL_dt(PURE_DECL(td)) == DCL_POINTER) {
        xcom::removehead(&PURE_DECL(td));
    }
    TREE_result_type(t) = td;
    return ST_SUCC;
}


static INT TypeTranCall(Tree * t, TYCtx * cont)
{
    ASSERT0(t);
    if (ST_SUCC != TypeTran(TREE_para_list(t), cont)) {
        return ST_ERR;
    }
    if (ST_SUCC != TypeTran(TREE_fun_exp(t), cont)) {
        return ST_ERR;
    }

    insertCvtForParams(t);
    Decl * ld = TREE_result_type(TREE_fun_exp(t));
    ASSERTN(DECL_dt(ld) == DCL_TYPE_NAME, ("expect TypeSpec-NAME"));
    ASSERT0(DECL_decl_list(ld));
    ASSERTN(DECL_dt(DECL_decl_list(ld)) == DCL_ABS_DECLARATOR,
            ("expect abs-declarator"));

    //Return value type is the CALL node type.
    //So constructing return value type.
    TypeSpec * ty = DECL_spec(ld);
    Decl * pure = PURE_DECL(ld);
    if (DECL_dt(pure) == DCL_FUN) {
        pure = DECL_next(pure);
    } else if (DECL_dt(pure) == DCL_POINTER &&
               DECL_next(pure) != nullptr &&
               DECL_dt(DECL_next(pure)) == DCL_FUN) {
        //FUN_POINTER
        pure = DECL_next(DECL_next(pure));
    }

    ASSERTN(pure == nullptr || DECL_dt(pure) != DCL_FUN, ("Illegal dcl list"));
    TREE_result_type(t) = buildTypeName(ty);
    PURE_DECL(TREE_result_type(t)) = pure;
    return ST_SUCC;
}


//The function handles token '+' '-'.
static INT TypeTranAdditive(Tree * t, TYCtx * cont)
{
    ASSERT0(t);
    if (ST_SUCC != TypeTran(TREE_lchild(t), cont)) {
        return ST_ERR;
    }

    if (ST_SUCC != TypeTran(TREE_rchild(t), cont)) {
        return ST_ERR;
    }

    Decl * ld = TREE_result_type(TREE_lchild(t));
    Decl * rd = TREE_result_type(TREE_rchild(t));
    if (TREE_token(t) == T_ADD) { // '+'
        if (is_pointer(ld) && is_pointer(rd)) {
            err(TREE_lineno(t), "can not add two pointers");
            return ST_ERR;
        }

        if (is_array(ld) && is_array(rd)) {
            err(TREE_lineno(t), "can not add two arrays");
            return ST_ERR;
        }

        if (!is_pointer(ld)) {
            if (is_struct(ld) || is_union(rd)) {
                err(TREE_lineno(t), "illegal '%s' for struct/union",
                    getTokenName(TREE_token(t)));
                return ST_ERR;
            }
        }

        if (is_pointer(rd)) {
            //Swap operands.
            Decl * tmp = ld;
            ld = rd;
            rd = tmp;
            TREE_result_type(TREE_lchild(t)) = ld;
            TREE_result_type(TREE_rchild(t)) = rd;
        }

        if (is_array(ld) && is_integer(rd)) {
            //Regard array type as pointer,
            //the result type is pointer.
            //e.g: int a[]; b=a+2; b is pointer.
            TREE_result_type(t) = buildPointerType(DECL_spec(ld));
        } else if (is_pointer(ld) && is_integer(rd)) {
            //Pointer arith.
            TREE_result_type(t) = ld;
        } else if (is_arith(ld) && is_arith(rd)) {
            //Arithmetic type.
            TREE_result_type(t) = buildBinaryOpType(TREE_type(t), ld, rd);
        } else {
            ASSERTN(0, ("illegal type for '%s'", getTokenName(TREE_token(t))));
        }
        return ST_SUCC; 
    }

    if (TREE_token(t) == T_SUB) { // '-'
        if (!is_pointer(ld) && is_pointer(rd)) {
            err(TREE_lineno(t),
                "pointer can only be subtracted from another pointer");
            return ST_ERR;
        }

        if (!is_pointer(ld)) {
            if (is_struct(DECL_spec(ld)) || is_union(DECL_spec(ld))) {
                err(TREE_lineno(t), "illegal '%s' for struct/union",
                    getTokenName(TREE_token(t)));
                return ST_ERR;
            }
        }

        if (!is_pointer(rd)) {
            if (is_struct(DECL_spec(rd)) || is_union(DECL_spec(rd))) {
                err(TREE_lineno(t), "illegal '%s' for struct/union",
                    getTokenName(TREE_token(t)));
                return ST_ERR;
            }
        }

        if (is_pointer(ld) && is_pointer(rd)) {
            //pointer - pointer
            TREE_result_type(t) = BUILD_TYNAME(T_SPEC_UNSIGNED | T_SPEC_LONG);
        } else if (is_pointer(ld) && is_integer(rd)) {
            //pointer - integer
            TREE_result_type(t) = ld;
        } else if (is_arith(ld) && is_arith(rd)) {
            //Arithmetic type
            TREE_result_type(t) = buildBinaryOpType(TREE_type(t), ld, rd);
        } else {
            ASSERTN(0, ("illegal type for '%s'",
                    getTokenName(TREE_token(t))));
        }
        return ST_SUCC; 
    }

    ASSERTN(0, ("illegal type for '%s'", getTokenName(TREE_token(t))));
    return ST_ERR; 
}


static INT TypeTranDeclInit(Decl const* decl, TYCtx * cont)
{
    for (Decl const* dcl = decl; dcl != nullptr; dcl = DECL_next(dcl)) {
        if (!is_initialized(dcl)) { continue; }
        Tree * inittree = get_decl_init_tree(dcl);
        ASSERT0(inittree);
        if (ST_SUCC != TypeTran(inittree, cont)) {
            return ST_ERR;
        } 
    }
    return ST_SUCC;
}


static INT TypeTranAssign(Tree * t, TYCtx * cont)
{
    ASSERT0(t);
    // one of   '='   '*='   '/='   '%='  '+='
    //          '-='  '<<='  '>>='  '&='  '^='  '|='
    if (ST_SUCC != TypeTran(TREE_lchild(t), cont)) {
        return ST_ERR;
    }
    if (ST_SUCC != TypeTran(TREE_rchild(t), cont)) {
        return ST_ERR;
    }
    if (!checkAssign(t, TREE_result_type(TREE_lchild(t)),
                     TREE_result_type(TREE_rchild(t)))) {
        return ST_ERR;
    }
    TREE_result_type(t) = TREE_result_type(TREE_lchild(t));
    return ST_SUCC;
}


static INT TypeTranBinaryLogical(Tree * t, TYCtx * cont)
{
    ASSERT0(t);
    if (ST_SUCC != TypeTran(TREE_lchild(t), cont)) { return ST_ERR; }
    if (ST_SUCC != TypeTran(TREE_rchild(t), cont)) { return ST_ERR; }

    Decl * ld = TREE_result_type(TREE_lchild(t));
    Decl * rd = TREE_result_type(TREE_rchild(t));
    if (is_pointer(ld) || is_array(ld)) {
        StrBuf buf(64);
        format_declaration(buf,ld);
        err(TREE_lineno(t), "illegal '%s', left operand has type '%s'",
            getTokenName(TREE_token(TREE_lchild(t))), buf.buf);
        return ST_ERR;
    }

    if (is_pointer(rd) || is_array(rd)) {
        StrBuf buf(64);
        format_declaration(buf,rd);
        err(TREE_lineno(t), "illegal '%s', right operand has type '%s'",
            getTokenName(TREE_token(TREE_rchild(t))), buf.buf);
        return ST_ERR;
    }

    if (is_struct(DECL_spec(ld)) || is_struct(DECL_spec(rd)) ||
        is_union(DECL_spec(ld)) || is_union(DECL_spec(rd))) {
        err(TREE_lineno(t), "illegal '%s' for struct/union",
            getTokenName(TREE_token(TREE_rchild(t))));
        return ST_ERR;
    }
    TREE_result_type(t) = buildBinaryOpType(TREE_type(t), ld, rd);
    return ST_SUCC;
}


static INT TypeTranBinaryRelation(Tree * t, TYCtx * cont)
{
    ASSERT0(t);
    if (ST_SUCC != TypeTran(TREE_lchild(t), cont)) { return ST_ERR; }
    if (ST_SUCC != TypeTran(TREE_rchild(t), cont)) { return ST_ERR; }

    Decl * ld = TREE_result_type(TREE_lchild(t));
    Decl * rd = TREE_result_type(TREE_rchild(t));
    ASSERT0(ld && rd);
    if ((is_struct(DECL_spec(ld)) || is_union(DECL_spec(ld))) &&
        !is_pointer(ld)) {
        err(TREE_lineno(t),
            "can not do '%s' operation for struct/union.",
            getTokenName(TREE_token(t)));
        return ST_ERR;
    }
    if ((is_struct(DECL_spec(rd)) || is_union(DECL_spec(rd))) &&
        !is_pointer(rd)) {
        err(TREE_lineno(t),
            "can not do '%s' operation for struct/union.",
            getTokenName(TREE_token(t)));
        return ST_ERR;
    }
    TREE_result_type(t) = BUILD_TYNAME(T_SPEC_UNSIGNED | T_SPEC_CHAR);
    return ST_SUCC;
}


static INT TypeTranInitValScope(Tree * t, TYCtx * cont)
{
    ASSERT0(t);
    ASSERT0(TREE_type(TREE_parent(t)) == TR_ASSIGN);
    ASSERT0(TREE_type(TREE_lchild(TREE_parent(t))) == TR_ID);
    Decl * decl = TREE_id_decl(TREE_lchild(TREE_parent(t)));
    ASSERT0(decl);
    ASSERT0(is_array(decl) || is_struct(decl) || is_union(decl));
    TypeTran(TREE_initval_scope(t), nullptr);
    TREE_result_type(t) = decl;
    return ST_SUCC;
}


//Transfering type declaration for all AST nodes.
static INT TypeTran(Tree * t, TYCtx * cont)
{
    TYCtx ct;
    if (cont == nullptr) {
        cont = &ct;
    }
    while (t != nullptr) {
        g_src_line_num = TREE_lineno(t);
        switch (TREE_type(t)) {
        case TR_ASSIGN:
            if (ST_SUCC != TypeTranAssign(t, cont)) { goto FAILED; }
            break;
        case TR_ID:
            if (ST_SUCC != TypeTranID(t, cont)) { goto FAILED; }
            break;
        case TR_IMM:
            if (GET_HIGH_32BIT(TREE_imm_val(t)) != 0) {
                TREE_result_type(t) = BUILD_TYNAME(T_SPEC_LONGLONG|T_QUA_CONST);
            } else {
                TREE_result_type(t) = BUILD_TYNAME(T_SPEC_INT|T_QUA_CONST);
            }
            break;
        case TR_IMMU:
            if (GET_HIGH_32BIT(TREE_imm_val(t)) != 0) {
                TREE_result_type(t) = BUILD_TYNAME(
                    T_SPEC_UNSIGNED|T_SPEC_LONGLONG|T_QUA_CONST);
            } else {
                TREE_result_type(t) = BUILD_TYNAME(
                    T_SPEC_UNSIGNED|T_SPEC_INT|T_QUA_CONST);
            }
            break;
        case TR_IMML:
            TREE_result_type(t) = BUILD_TYNAME(T_SPEC_LONGLONG|T_QUA_CONST);
            break;
        case TR_IMMUL:
            TREE_result_type(t) = BUILD_TYNAME(
                T_SPEC_UNSIGNED|T_SPEC_LONGLONG|T_QUA_CONST);
            break;
        case TR_FP:
        case TR_FPLD:
            TREE_result_type(t) = BUILD_TYNAME(T_SPEC_DOUBLE|T_QUA_CONST);
            break;
        case TR_FPF:
            TREE_result_type(t) = BUILD_TYNAME(T_SPEC_FLOAT|T_QUA_CONST);
            break;
        case TR_ENUM_CONST:
            TREE_result_type(t) = BUILD_TYNAME(T_SPEC_ENUM|T_QUA_CONST);
            break;
        case TR_STRING: {
            Decl * tn = BUILD_TYNAME(T_SPEC_CHAR|T_QUA_CONST);
            Decl * d = new_decl(DCL_ARRAY);
            ASSERT0(TREE_string_val(t));
            DECL_array_dim(d) = strlen(SYM_name(TREE_string_val(t))) + 1;
            xcom::add_next(&PURE_DECL(tn), d);
            TREE_result_type(t) = tn;
            break;
        }
        case TR_LOGIC_OR: //logical or ||
        case TR_LOGIC_AND: //logical and &&
            if (ST_SUCC != TypeTran(TREE_lchild(t), cont)) { goto FAILED; }
            if (ST_SUCC != TypeTran(TREE_rchild(t), cont)) { goto FAILED; }
            TREE_result_type(t) = BUILD_TYNAME(T_SPEC_UNSIGNED|T_SPEC_CHAR);
            break;
        case TR_INCLUSIVE_OR: //inclusive or |
        case TR_XOR: //exclusive or
        case TR_INCLUSIVE_AND: //inclusive and &
        case TR_SHIFT: // >> <<
            if (ST_SUCC != TypeTranBinaryLogical(t, cont)) { goto FAILED; }
            break;
        case TR_EQUALITY: // == !=
        case TR_RELATION: // < > >= <=
            if (ST_SUCC != TypeTranBinaryRelation(t, cont)) { goto FAILED; }
            break;
        case TR_ADDITIVE: // '+' '-'
            if (ST_SUCC != TypeTranAdditive(t, cont)) { goto FAILED; }
            break;
        case TR_MULTI: // '*' '/' '%'
            if (ST_SUCC != TypeTranMulti(t, cont)) { goto FAILED; }
            break;
        case TR_INITVAL_SCOPE:
            if (ST_SUCC != TypeTranInitValScope(t, cont)) { goto FAILED; }
            break;
        case TR_SCOPE: {
            Scope * sc = TREE_scope(t);
            if (ST_SUCC != TypeTranDeclInit(SCOPE_decl_list(sc), nullptr)) {
                goto FAILED;
            }
            if (ST_SUCC != TypeTran(SCOPE_stmt_list(sc), nullptr)) {
                goto FAILED;
            }
            break;
        }
        case TR_IF:
            if (ST_SUCC != TypeTran(TREE_if_det(t), cont)) { goto FAILED; }
            if (ST_SUCC != TypeTran(TREE_if_true_stmt(t), cont)) {
                goto FAILED;
            }
            if (ST_SUCC != TypeTran(TREE_if_false_stmt(t), cont)) {
                goto FAILED;
            }
            break;
        case TR_DO:
            if (ST_SUCC != TypeTran(TREE_dowhile_det(t), cont)) {
                goto FAILED;
            }
            if (ST_SUCC != TypeTran(TREE_dowhile_body(t), cont)) {
                goto FAILED;
            }
            break;
        case TR_WHILE:
            if (ST_SUCC != TypeTran(TREE_whiledo_det(t), cont)) {
                goto FAILED;
            }
            if (ST_SUCC != TypeTran(TREE_whiledo_body(t), cont)) {
                goto FAILED;
            }
            break;
        case TR_FOR:
            if (TREE_for_scope(t) != nullptr &&
                ST_SUCC != TypeTranDeclInit(
                    SCOPE_decl_list(TREE_for_scope(t)), nullptr)) {
                goto FAILED;
            }
            if (ST_SUCC != TypeTran(TREE_for_init(t), nullptr)) {
                goto FAILED;
            }
            if (ST_SUCC != TypeTran(TREE_for_det(t), nullptr)) {
                goto FAILED;
            }
            if (ST_SUCC != TypeTran(TREE_for_step(t), nullptr)) {
                goto FAILED;
            }
            if (ST_SUCC != TypeTran(TREE_for_body(t), nullptr)) {
                goto FAILED;
            }
            break;
        case TR_SWITCH:
            if (ST_SUCC != TypeTran(TREE_switch_det(t), nullptr)) {
                goto FAILED; 
            }
            if (ST_SUCC != TypeTran(TREE_switch_body(t), nullptr)) {
                goto FAILED;
            }
            break;
        case TR_BREAK:
        case TR_CONTINUE:
        case TR_GOTO:
        case TR_LABEL:
        case TR_DEFAULT:
        case TR_CASE:
            break;
        case TR_RETURN:
            if (ST_SUCC != TypeTran(TREE_ret_exp(t), cont)) { goto FAILED; }
            break;
        case TR_COND: //formulized log_OR_exp?exp:cond_exp
            if (ST_SUCC != TypeTranCond(t, cont)) { goto FAILED; }
            break;
        case TR_CVT: { //type convertion
            if (ST_SUCC != TypeTran(TREE_cast_exp(t), cont)) { goto FAILED; }

            Decl * type_name = TREE_type_name(TREE_cvt_type(t));
            if (IS_USER_TYPE_REF(DECL_spec(type_name))) {
                //Expand the combined type here.
                type_name = expand_user_type(type_name);
                ASSERTN(is_valid_type_name(type_name),
                        ("Illegal expanding user-type"));
            }
            TREE_result_type(t) = type_name;
            break;
        }
        case TR_TYPE_NAME: //user defined type or C standard type
            //TR_TYPE_NAME node should be process by its parent node directly.
            ASSERTN(0, ("Should not be arrival"));
            break;
        case TR_LDA: {  // &a get address of 'a'
            if (ST_SUCC != TypeTran(TREE_lchild(t), cont)) goto FAILED;
            Decl * ld = TREE_result_type(TREE_lchild(t));
            Decl * td = cp_type_name(ld);
            insertafter(&PURE_DECL(td), new_decl(DCL_POINTER));
            TREE_result_type(t) = td;
            break;
        }
        case TR_DEREF: // *p dereferencing the pointer 'p'
            if (ST_SUCC != TypeTranDeref(t, cont)) { goto FAILED; }
            break;
        case TR_PLUS: // +123
        case TR_MINUS: { // -123
            if (ST_SUCC != TypeTran(TREE_lchild(t), cont)) goto FAILED;
            Decl * ld = TREE_result_type(TREE_lchild(t));
            if (!is_arith(ld) || is_array(ld) || is_pointer(ld)) {
                StrBuf buf(64);
                format_declaration(buf,ld);
                if (TREE_type(t) == TR_PLUS) {
                    err(TREE_lineno(t),
                        "illegal positive '+' for type '%s'", buf.buf);
                } else {
                    err(TREE_lineno(t),
                        "illegal minus '-' for type '%s'", buf.buf);
                }
            }
            TREE_result_type(t) = ld;
            break;
        }
        case TR_REV: { // reverse
            if (ST_SUCC != TypeTran(TREE_lchild(t), cont)) goto FAILED;
            Decl * ld = TREE_result_type(TREE_lchild(t));
            if (!is_integer(ld) || is_array(ld) || is_pointer(ld)) {
                StrBuf buf(64);
                format_declaration(buf,ld);
                err(TREE_lineno(t),
                    "illegal bit reverse operation for type '%s'", buf.buf);
            }
            TREE_result_type(t) = ld;
            break;
        }
        case TR_NOT: { // get non-value
            if (ST_SUCC != TypeTran(TREE_lchild(t), cont)) goto FAILED;

            Decl * ld = TREE_result_type(TREE_lchild(t));
            if (!is_arith(ld) && !is_pointer(ld)) {
                StrBuf buf(64);
                format_declaration(buf, ld);
                err(TREE_lineno(t),
                    "illegal logical not operation for type '%s'", buf.buf);
            }
            TREE_result_type(t) = ld;
            break;
        }
        case TR_INC:   //++a
        case TR_POST_INC: //a++
            if (ST_SUCC != TypeTranPreAndPostInc(t, cont)) { goto FAILED; }
            break;
        case TR_DEC: //--a
        case TR_POST_DEC: //a--
            if (ST_SUCC != TypeTranPreAndPostDec(t, cont)) { goto FAILED; }
            break;
        case TR_SIZEOF: // sizeof(a)
            if (ST_SUCC != TypeTranSizeof(t, cont)) { goto FAILED; }
            break;
        case TR_CALL:
            if (ST_SUCC != TypeTranCall(t, cont)) { goto FAILED; }
            break;
        case TR_ARRAY:
            if (ST_SUCC != TypeTranArray(t, cont)) { goto FAILED; }
            break;
        case TR_DMEM: // a.b
            if (ST_SUCC != TypeTranDMem(t, cont)) { goto FAILED; }
            break;
        case TR_INDMEM: // a->b
            if (ST_SUCC != TypeTranInDMem(t, cont)) { goto FAILED; }
            break;
        case TR_PRAGMA:
        case TR_PREP:
            break;
        default: ASSERTN(0, ("unknown tree type:%d", TREE_type(t)));
        }
        t = TREE_nsib(t);
    }
    return ST_SUCC;
FAILED:
    return ST_ERR;
}


//Declaration checking
static INT checkDeclaration(Decl const* d)
{
    ASSERT0(d && DECL_dt(d) == DCL_DECLARATION);
    Decl const* dclor = get_pure_declarator(d);
    ASSERT0(dclor);
    while (dclor != nullptr) {
        if (DECL_dt(dclor) == DCL_FUN) {
            Decl * ret_value_type = DECL_next(dclor);
            if (ret_value_type) {
                if (DECL_dt(ret_value_type) == DCL_FUN) {
                    err(g_real_line_num,
                        "return value type of function can not be a function");
                    return ST_ERR;
                }
                if (DECL_dt(ret_value_type) == DCL_ARRAY) {
                    err(g_real_line_num,
                        "return value type of function can not be an array");
                    return ST_ERR;
                }
            }
        }
        dclor = DECL_next(dclor);
    }
    return ST_SUCC;
}


void initTypeTran()
{
    g_schar_type = new_type(T_SPEC_SIGNED | T_SPEC_CHAR);
    g_sshort_type = new_type(T_SPEC_SIGNED | T_SPEC_SHORT);
    g_sint_type = new_type(T_SPEC_SIGNED | T_SPEC_INT);
    g_slong_type = new_type(T_SPEC_SIGNED | T_SPEC_LONG);
    g_slonglong_type = new_type(T_SPEC_SIGNED | T_SPEC_LONGLONG);

    g_uchar_type = new_type(T_SPEC_UNSIGNED | T_SPEC_CHAR);
    g_ushort_type = new_type(T_SPEC_UNSIGNED | T_SPEC_SHORT);
    g_uint_type = new_type(T_SPEC_UNSIGNED | T_SPEC_INT);
    g_ulong_type = new_type(T_SPEC_UNSIGNED | T_SPEC_LONG);
    g_ulonglong_type = new_type(T_SPEC_UNSIGNED | T_SPEC_LONGLONG);

    g_float_type = new_type(T_SPEC_FLOAT);
    g_double_type = new_type(T_SPEC_DOUBLE);
    g_void_type = new_type(T_SPEC_VOID);
    g_enum_type = new_type(T_SPEC_ENUM);
}


//Infer type to tree nodes.
INT TypeTransform()
{
    initTypeTran();
    Scope * s = get_global_scope();
    Decl * dcl = SCOPE_decl_list(s);
    while (dcl != nullptr) {
        ASSERT0(DECL_decl_scope(dcl) == s);
        if (DECL_is_fun_def(dcl)) {
            Tree * stmt = SCOPE_stmt_list(DECL_fun_body(dcl));
            if (ST_SUCC != TypeTran(stmt, nullptr)) {
                return ST_ERR;
            }
            if (g_err_msg_list.get_elem_count() > 0) {
                return ST_ERR;
            }
        }
        dcl = DECL_next(dcl);
    }
    return ST_SUCC;
}
