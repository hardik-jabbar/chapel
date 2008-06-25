#include "astutil.h"
#include "bb.h"
#include "expr.h"
#include "stmt.h"
#include "iterator.h"
#include "optimizations.h"
#include "view.h"


static void
updateOneSymbol(FnSymbol* fn, Symbol* s1, Symbol* s2) {
  ASTMap map;
  map.put(s1, s2);
  update_symbols(fn, &map);
}


//#define DEBUG_LIVE


IteratorInfo::IteratorInfo() :
  classType(NULL),
  getHeadCursor(NULL),
  getNextCursor(NULL),
  isValidCursor(NULL),
  getValue(NULL)
{}


static FnSymbol*
buildEmptyIteratorMethod(const char* name, ClassType* ct) {
  FnSymbol* fn = new FnSymbol(name);
  fn->copyPragmas(fn);
  fn->addPragma("auto ii"); 
  fn->global = true;
  fn->insertFormalAtTail(new ArgSymbol(INTENT_BLANK, "_mt", dtMethodToken));
  fn->_this = new ArgSymbol(INTENT_BLANK, "this", ct);
  fn->insertFormalAtTail(fn->_this);
  return fn;
}


static VarSymbol* newTemp(FnSymbol* fn, Type* type, const char* name = "_tmp") {
  VarSymbol* var = new VarSymbol(name, type);
  var->isCompilerTemp = true;
  fn->insertAtHead(new DefExpr(var));
  return var;
}


void prototypeIteratorClass(FnSymbol* fn) {
  currentLineno = fn->lineno;

  IteratorInfo* ii = new IteratorInfo();
  fn->iteratorInfo = ii;

  ii->classType = new ClassType(CLASS_CLASS);
  const char* className = astr("_ic_", fn->name);
  if (fn->_this)
    className = astr(className, "_", fn->_this->type->symbol->cname);
  TypeSymbol* cts = new TypeSymbol(className, ii->classType);
  cts->addPragma("iterator class");
  cts->addPragma("no object");
  if (fn->retTag == RET_VAR)
    cts->addPragma("ref iterator class");
  fn->defPoint->insertBefore(new DefExpr(cts));

  Type* cursorType = dtInt[INT_SIZE_32];
  ii->getHeadCursor = buildEmptyIteratorMethod("getHeadCursor", ii->classType);
  ii->getHeadCursor->retType = cursorType;

  ii->getNextCursor = buildEmptyIteratorMethod("getNextCursor", ii->classType);
  ii->getNextCursor->retType = cursorType;
  ii->getNextCursor->insertFormalAtTail(
    new ArgSymbol(INTENT_BLANK, "cursor", cursorType));

  ii->isValidCursor = buildEmptyIteratorMethod("isValidCursor", ii->classType);
  ii->isValidCursor->retType = dtBool;
  ii->isValidCursor->insertFormalAtTail(
    new ArgSymbol(INTENT_BLANK, "cursor", cursorType));

  ii->getValue = buildEmptyIteratorMethod("getValue", ii->classType);
  if (fn->retTag == RET_VAR && fn->retType->refType)
    ii->getValue->retType = fn->retType->refType; // unexecuted none/gasnet on 4/25/08
  else
    ii->getValue->retType = fn->retType;
  ii->getValue->insertFormalAtTail(
    new ArgSymbol(INTENT_BLANK, "cursor", cursorType));

  ii->getZipCursor1 = buildEmptyIteratorMethod("getZipCursor1", ii->classType);
  ii->getZipCursor1->retType = cursorType;

  ii->getZipCursor2 = buildEmptyIteratorMethod("getZipCursor2", ii->classType);
  ii->getZipCursor2->retType = cursorType;
  ii->getZipCursor2->insertFormalAtTail(
    new ArgSymbol(INTENT_BLANK, "cursor", cursorType));

  ii->getZipCursor3 = buildEmptyIteratorMethod("getZipCursor3", ii->classType);
  ii->getZipCursor3->retType = cursorType;
  ii->getZipCursor3->insertFormalAtTail(
    new ArgSymbol(INTENT_BLANK, "cursor", cursorType));

  ii->getZipCursor4 = buildEmptyIteratorMethod("getZipCursor4", ii->classType);
  ii->getZipCursor4->retType = cursorType;
  ii->getZipCursor4->insertFormalAtTail(
    new ArgSymbol(INTENT_BLANK, "cursor", cursorType));

  fn->defPoint->insertBefore(new DefExpr(ii->getHeadCursor));
  fn->defPoint->insertBefore(new DefExpr(ii->getNextCursor));
  fn->defPoint->insertBefore(new DefExpr(ii->isValidCursor));
  fn->defPoint->insertBefore(new DefExpr(ii->getValue));
  fn->defPoint->insertBefore(new DefExpr(ii->getZipCursor1));
  fn->defPoint->insertBefore(new DefExpr(ii->getZipCursor2));
  fn->defPoint->insertBefore(new DefExpr(ii->getZipCursor3));
  fn->defPoint->insertBefore(new DefExpr(ii->getZipCursor4));

  ii->classType->defaultConstructor = fn;
  ii->classType->scalarPromotionType = fn->retType;
  fn->retType = ii->classType;
  fn->retTag = RET_VALUE;
}


//
// insert "v = &ic.f" or "v = ic.f" at head of fn
//
static void
insertGetMember(FnSymbol* fn, BaseAST* v, Symbol* ic, Symbol* f) {
  Symbol* local = toSymbol(v);
  INT_ASSERT(local);

  PrimitiveTag primitiveGetMemberTag;
  if (local->type == f->type->refType)
    primitiveGetMemberTag = PRIMITIVE_GET_MEMBER;
  else
    primitiveGetMemberTag = PRIMITIVE_GET_MEMBER_VALUE;
  
  fn->insertAtHead(
    new CallExpr(PRIMITIVE_MOVE, local,
      new CallExpr(primitiveGetMemberTag, ic, f)));
}


//
// when ast is a function fn
//   insert "ic.f = v" or "t = &v; ic.f = t" at tail of fn
// when ast is an expression expr
//   insert "ic.f = v" or "t = &v; ic.f = t" after expr
//
static void
insertSetMember(BaseAST* ast, Symbol* ic, Symbol* f, BaseAST* v) {
  Symbol* local = toSymbol(v);
  INT_ASSERT(local);
  
  if (FnSymbol* fn = toFnSymbol(ast)) {
    if (local->type == f->type->refType) {
      Symbol* tmp = newTemp(fn, f->type);
      fn->insertAtTail(
        new CallExpr(PRIMITIVE_MOVE, tmp,
          new CallExpr(PRIMITIVE_GET_REF, local)));
      fn->insertAtTail(new CallExpr(PRIMITIVE_SET_MEMBER, ic, f, tmp));
    } else
      fn->insertAtTail(new CallExpr(PRIMITIVE_SET_MEMBER, ic, f, local));
  } else if (Expr* expr = toExpr(ast)) {
    if (local->type == f->type->refType) {
      Symbol* tmp = newTemp(expr->getFunction(), f->type);
      expr->getStmtExpr()->insertAfter(
        new CallExpr(PRIMITIVE_SET_MEMBER, ic, f, tmp));
      expr->getStmtExpr()->insertAfter(
        new CallExpr(PRIMITIVE_MOVE, tmp,
          new CallExpr(PRIMITIVE_GET_REF, local)));
    } else
      expr->getStmtExpr()->insertAfter(
        new CallExpr(PRIMITIVE_SET_MEMBER, ic, f, local));
  } else
    INT_FATAL(ast, "unexpected case in insertSetMember");
}


//
// initialize temp to default value (recursive for records)
//
static void
insertSetMemberInits(FnSymbol* fn, Symbol* var) {
  Type* type = var->type;
  if (type->symbol->hasPragma("ref"))
    type = getValueType(type); // unexecuted none/gasnet on 4/25/08
  if (type->defaultValue) {
    fn->insertAtTail(new CallExpr(PRIMITIVE_MOVE, var, type->defaultValue));
  } else {
    ClassType* ct = toClassType(type);
    INT_ASSERT(ct);
    for_fields(field, ct) {
      if (field->type->symbol->hasPragma("ref")) {
        if (getValueType(field->type)->symbol->hasPragma("array"))
          continue; // skips array types
        Symbol* tmp = new VarSymbol("_tmp", field->type);
        tmp->isCompilerTemp = true;
        fn->insertAtTail(new DefExpr(tmp));
        fn->insertAtTail(new CallExpr(PRIMITIVE_MOVE, tmp, gNilRef));
        fn->insertAtTail(new CallExpr(PRIMITIVE_SET_MEMBER, var, field, tmp));
      } else if (field->type->refType) { // skips array types (how to handle arrays?) ( sjd later: really? )
        Symbol* tmp = new VarSymbol("_tmp", field->type);
        tmp->isCompilerTemp = true;
        fn->insertAtTail(new DefExpr(tmp));
        insertSetMemberInits(fn, tmp);
        fn->insertAtTail(new CallExpr(PRIMITIVE_SET_MEMBER, var, field, tmp));
      }
    }
  }
}


static void
buildGetNextCursor(FnSymbol* fn,
                   Vec<BaseAST*>& asts,
                   Map<Symbol*,Symbol*>& local2field,
                   Vec<Symbol*>& locals,
                   Map<Symbol*,Vec<SymExpr*>*>& defMap,
                   Map<Symbol*,Vec<SymExpr*>*>& useMap) {
  IteratorInfo* ii = fn->iteratorInfo;
  Symbol *iterator, *cursor, *t1;

  Vec<Symbol*> labels;
  iterator = ii->getNextCursor->getFormal(1);
  cursor = ii->getNextCursor->getFormal(2);
  for_alist(expr, fn->body->body)
    ii->getNextCursor->insertAtTail(expr->remove());
  Symbol* end = new LabelSymbol("_end");

  // change yields to labels and gotos
  int i = 2; // 1 = not started, 0 = finished
  forv_Vec(BaseAST, ast, asts) {
    if (CallExpr* call = toCallExpr(ast)) {
      if (call->isPrimitive(PRIMITIVE_YIELD)) {
        call->insertBefore(new CallExpr(PRIMITIVE_MOVE, cursor, new_IntSymbol(i)));
        call->insertBefore(new GotoStmt(GOTO_NORMAL, end));
        Symbol* label = new LabelSymbol(astr("_jump_", istr(i)));
        call->insertBefore(new DefExpr(label));
        labels.add(label);
        call->remove();
        i++;
      } else if (call->isPrimitive(PRIMITIVE_RETURN)) {
        call->insertBefore(new CallExpr(PRIMITIVE_MOVE, cursor, new_IntSymbol(0)));
        call->remove(); // remove old return
      }
    }
  }
  ii->getNextCursor->insertAtTail(new DefExpr(end));

  // insert jump table at head of getNextCursor
  i = 2;
  t1 = newTemp(ii->getNextCursor, dtBool);
  forv_Vec(Symbol, label, labels) {
    ii->getNextCursor->insertAtHead(new CondStmt(new SymExpr(t1), new GotoStmt(GOTO_NORMAL, label)));
    ii->getNextCursor->insertAtHead(new CallExpr(PRIMITIVE_MOVE, t1, new CallExpr(PRIMITIVE_EQUAL, cursor, new_IntSymbol(i++))));
  }

  // load local variables from fields at return points and update
  // fields when local variables change
  forv_Vec(Symbol, local, locals) {
    Symbol* field = local2field.get(local);
    if (toArgSymbol(local)) {
      Type* type = local->type;
      Symbol* newlocal = newTemp(ii->getNextCursor, type, local->name);
      ASTMap map;
      map.put(local, newlocal);
      update_symbols(ii->getNextCursor, &map);
      local = newlocal;
    }
    insertGetMember(ii->getNextCursor, local, iterator, field);
    if (isRecordType(local->type)) {
      insertSetMember(ii->getNextCursor, iterator, field, local);
    } else {
      for_defs(se, defMap, local) {
        if (toCallExpr(se->parentExpr))
          insertSetMember(se, iterator, field, local);
      }

      // update based on indirect writes via references
      for_uses(se, useMap, local) {
        if (CallExpr* ref = toCallExpr(se->parentExpr))
          if (ref->isPrimitive(PRIMITIVE_SET_REF))
            if (CallExpr* move = toCallExpr(ref->parentExpr))
              if (move->isPrimitive(PRIMITIVE_MOVE))
                if (SymExpr* lhs = toSymExpr(move->get(1)))
                  for_defs(se, defMap, lhs->var) {
                    if (toCallExpr(se->parentExpr))
                      insertSetMember(se, iterator, field, local);
                  }
      }
    }
  }
  t1 = newTemp(ii->getNextCursor, ii->getNextCursor->retType);
  ii->getNextCursor->insertAtTail(new CallExpr(PRIMITIVE_MOVE, t1, cursor));
  ii->getNextCursor->insertAtTail(new CallExpr(PRIMITIVE_RETURN, t1));
}


static void
buildGetHeadCursor(FnSymbol* fn) {
  IteratorInfo* ii = fn->iteratorInfo;
  Symbol *iterator, *t1;
  iterator = ii->getHeadCursor->getFormal(1);
  t1 = newTemp(ii->getHeadCursor, ii->getHeadCursor->retType);
  ii->getHeadCursor->insertAtTail(new CallExpr(PRIMITIVE_MOVE, t1, new CallExpr(ii->getNextCursor, iterator, new_IntSymbol(1))));
  ii->getHeadCursor->insertAtTail(new CallExpr(PRIMITIVE_RETURN, t1));
}


static void
buildIsValidCursor(FnSymbol* fn) {
  IteratorInfo* ii = fn->iteratorInfo;
  Symbol *cursor, *t1;
  cursor = ii->isValidCursor->getFormal(2);
  t1 = newTemp(ii->isValidCursor, dtBool);
  ii->isValidCursor->insertAtTail(new CallExpr(PRIMITIVE_MOVE, t1, new CallExpr(PRIMITIVE_NOTEQUAL, cursor, new_IntSymbol(0))));
  ii->isValidCursor->insertAtTail(new CallExpr(PRIMITIVE_RETURN, t1));
}


static void
buildGetValue(FnSymbol* fn, Symbol* value) {
  IteratorInfo* ii = fn->iteratorInfo;
  Symbol *iterator, *t1;
  iterator = ii->getValue->getFormal(1);
  t1 = newTemp(ii->getValue, ii->getValue->retType);
  ii->getValue->insertAtTail(
    new CallExpr(PRIMITIVE_MOVE, t1,
      new CallExpr(PRIMITIVE_GET_MEMBER_VALUE, iterator, value)));
  ii->getValue->insertAtTail(new CallExpr(PRIMITIVE_RETURN, t1));
}


static void
buildDefaultZipMethods(FnSymbol* fn) {
  IteratorInfo* ii = fn->iteratorInfo;
  Symbol *iterator, *cursor, *t1;

  //
  // getZipCursor1 == getHeadCursor
  //
  iterator = ii->getZipCursor1->getFormal(1);
  t1 = newTemp(ii->getZipCursor1, ii->getZipCursor1->retType);
  ii->getZipCursor1->insertAtTail(
    new CallExpr(PRIMITIVE_MOVE, t1,
      new CallExpr(ii->getHeadCursor, iterator)));
  ii->getZipCursor1->insertAtTail(new CallExpr(PRIMITIVE_RETURN, t1));
  ii->getZipCursor1->addPragma("inline");

  //
  // getZipCursor2 is NOOP
  //
  iterator = ii->getZipCursor2->getFormal(1);
  cursor = ii->getZipCursor2->getFormal(2);
  t1 = newTemp(ii->getZipCursor2, ii->getZipCursor2->retType);
  ii->getZipCursor2->insertAtTail(new CallExpr(PRIMITIVE_MOVE, t1, cursor));
  ii->getZipCursor2->insertAtTail(new CallExpr(PRIMITIVE_RETURN, t1));
  ii->getZipCursor2->addPragma("inline");

  //
  // getZipCursor3 == getNextCursor
  //
  iterator = ii->getZipCursor3->getFormal(1);
  cursor = ii->getZipCursor3->getFormal(2);
  t1 = newTemp(ii->getZipCursor3, ii->getZipCursor3->retType);
  ii->getZipCursor3->insertAtTail(
    new CallExpr(PRIMITIVE_MOVE, t1,
      new CallExpr(ii->getNextCursor, iterator, cursor)));
  ii->getZipCursor3->insertAtTail(new CallExpr(PRIMITIVE_RETURN, t1));
  ii->getZipCursor3->addPragma("inline");

  //
  // getZipCursor4 is NOOP
  //
  iterator = ii->getZipCursor4->getFormal(1);
  cursor = ii->getZipCursor4->getFormal(2);
  t1 = newTemp(ii->getZipCursor4, ii->getZipCursor4->retType);
  ii->getZipCursor4->insertAtTail(new CallExpr(PRIMITIVE_MOVE, t1, cursor));
  ii->getZipCursor4->insertAtTail(new CallExpr(PRIMITIVE_RETURN, t1));
  ii->getZipCursor4->addPragma("inline");
}


//
// Determines that an iterator has a single loop with a single yield
// in it by checking the following conditions:
//
//   1. There is exactly one for-loop and no other loops.
//   2. The single for-loop is top-level to the function.
//   3. There is exactly one yield.
//   4. The single yield is top-level to the for-loop.
//   5. There are no goto statements.
//
// I believe these conditions can be relaxed.
//
static CallExpr*
isSingleLoopIterator(FnSymbol* fn, Vec<BaseAST*>& asts) {
  BlockStmt* singleFor = NULL;
  CallExpr* singleYield = NULL;
  forv_Vec(BaseAST, ast, asts) {
    if (CallExpr* call = toCallExpr(ast)) {
      if (call->isPrimitive(PRIMITIVE_YIELD)) {
        if (singleYield) {
          return NULL;
        } else if (BlockStmt* block = toBlockStmt(call->parentExpr)) {
          if (block->loopInfo &&
              (block->loopInfo->isPrimitive(PRIMITIVE_LOOP_FOR) ||
               block->loopInfo->isPrimitive(PRIMITIVE_LOOP_C_FOR) ||
               block->loopInfo->isPrimitive(PRIMITIVE_LOOP_WHILEDO))) {
            singleYield = call;
          } else {
            return NULL;
          }
        } else {
          return NULL;
        }
      }
    } else if (BlockStmt* block = toBlockStmt(ast)) {
      if (block->loopInfo) {
        if (singleFor) {
          return NULL;
        } else if ((block->loopInfo->isPrimitive(PRIMITIVE_LOOP_FOR) ||
                    block->loopInfo->isPrimitive(PRIMITIVE_LOOP_C_FOR) ||
                    block->loopInfo->isPrimitive(PRIMITIVE_LOOP_WHILEDO)) &&
                   block->parentExpr == fn->body) {
          singleFor = block;
        } else {
          return NULL;
        }
      }
    } else if (ast->astTag == STMT_GOTO) {
      return NULL;
    }
  }
  if (singleFor && singleYield)
    return singleYield;
  else
    return NULL;
}


//
// Builds the iterator interface methods for a single loop iterator as
// determined by isSingleLoopIterator.
//
// A single loop iterator has the form:
//
//  iterator foo() {
//    BLOCK I
//    for loop {
//      BLOCK II
//      yield statement
//      BLOCK III
//    }
//    BLOCK IV
//  }
//
static void
buildSingleLoopMethods(FnSymbol* fn,
                       Vec<BaseAST*>& asts,
                       Map<Symbol*,Symbol*>& local2field,
                       Vec<Symbol*>& locals,
                       Symbol* value,
                       CallExpr* yield,
                       Map<Symbol*,Vec<SymExpr*>*>& defMap,
                       Map<Symbol*,Vec<SymExpr*>*>& useMap) {
  IteratorInfo* ii = fn->iteratorInfo;
  BlockStmt* loop = toBlockStmt(yield->parentExpr);

  Symbol* headIterator = ii->getHeadCursor->getFormal(1);
  Symbol* nextIterator = ii->getNextCursor->getFormal(1);
  Symbol* zip1Iterator = ii->getZipCursor1->getFormal(1);
  Symbol* zip2Iterator = ii->getZipCursor2->getFormal(1);
  Symbol* zip3Iterator = ii->getZipCursor3->getFormal(1);
  Symbol* zip4Iterator = ii->getZipCursor4->getFormal(1);

  ASTMap headMap; // copy map of iterator to getHeadCursor
  ASTMap zip1Map; // copy map of iterator to getZipCursor1
  ASTMap zip2Map; // copy map of iterator to getZipCursor2
  ASTMap zip3Map; // copy map of iterator to getZipCursor3
  ASTMap zip4Map; // copy map of iterator to getZipCursor4
                  // note: there is no map for getNextCursor since the
                  // asts are moved (not copied) to getNextCursor

  //
  // add local variable defs to iterator methods that need them
  //
  forv_Vec(BaseAST, ast, asts) {
    if (DefExpr* def = toDefExpr(ast)) {
      if (toArgSymbol(def->sym))
        continue;
      ii->getNextCursor->insertAtHead(def->remove());
      ii->getHeadCursor->insertAtHead(def->copy(&headMap));
      ii->getZipCursor1->insertAtHead(def->copy(&zip1Map));
      ii->getZipCursor2->insertAtHead(def->copy(&zip2Map));
      ii->getZipCursor3->insertAtHead(def->copy(&zip3Map));
      ii->getZipCursor4->insertAtHead(def->copy(&zip4Map));
    }
  }

  //
  // add BLOCK I to getHeadCursor method
  // copy BLOCK I to getZipCursor1 method
  //
  for_alist(expr, fn->body->body) {
    if (expr == loop)
      break;
    ii->getHeadCursor->insertAtTail(expr->copy(&headMap));
    ii->getZipCursor1->insertAtTail(expr->copy(&zip1Map));
    expr->remove();
  }

  //
  // add BLOCK III to getNextCursor method
  // add BLOCK III to getZipCursor3 method
  //
  bool postYield = false;
  for_alist(expr, loop->body) {
    if (!postYield) {
      if (expr == yield)
        postYield = true;
      continue;
    }
    ii->getNextCursor->insertAtTail(expr->remove());
    ii->getZipCursor3->insertAtTail(expr->copy(&zip3Map));
  }

  Symbol* cloopHeadCond = NULL;
  Symbol* cloopNextCond = NULL;
  Symbol* cloopZip2Cond = NULL;
  Symbol* cloopZip4Cond = NULL;
  if (loop->loopInfo->isPrimitive(PRIMITIVE_LOOP_C_FOR)) {
    cloopHeadCond = new VarSymbol("_cond", dtBool);
    cloopNextCond = new VarSymbol("_cond", dtBool);
    cloopZip2Cond = new VarSymbol("_cond", dtBool);
    cloopZip4Cond = new VarSymbol("_cond", dtBool);
    ii->getHeadCursor->insertAtTail(new DefExpr(cloopHeadCond));
    ii->getNextCursor->insertAtTail(new DefExpr(cloopNextCond));
    ii->getZipCursor2->insertAtTail(new DefExpr(cloopZip2Cond));
    ii->getZipCursor4->insertAtTail(new DefExpr(cloopZip4Cond));
    ii->getHeadCursor->insertAtTail(new CallExpr(PRIMITIVE_MOVE, loop->loopInfo->get(1)->copy(&headMap), loop->loopInfo->get(2)->copy(&headMap)));
    ii->getHeadCursor->insertAtTail(new CallExpr(PRIMITIVE_MOVE, cloopHeadCond, new CallExpr(PRIMITIVE_LESSOREQUAL, loop->loopInfo->get(1)->copy(&headMap), loop->loopInfo->get(3)->copy(&headMap))));

    Symbol* counter = toSymExpr(loop->loopInfo->get(1))->var;
    ii->getZipCursor1->insertAtTail(new CallExpr(PRIMITIVE_MOVE, zip1Map.get(counter), loop->loopInfo->get(2)->copy(&zip1Map)));
    ii->getZipCursor1->insertAtTail(new CallExpr(PRIMITIVE_SET_MEMBER, zip1Iterator, local2field.get(counter), zip1Map.get(counter)));

    ii->getNextCursor->insertAtTail(new CallExpr(PRIMITIVE_MOVE, loop->loopInfo->get(1)->copy(), new CallExpr(PRIMITIVE_ADD, loop->loopInfo->get(1)->copy(), loop->loopInfo->get(4)->copy())));
    ii->getNextCursor->insertAtTail(new CallExpr(PRIMITIVE_MOVE, cloopNextCond, new CallExpr(PRIMITIVE_LESSOREQUAL, loop->loopInfo->get(1)->copy(), loop->loopInfo->get(3)->copy())));
    ii->getZipCursor3->insertAtTail(new CallExpr(PRIMITIVE_MOVE, loop->loopInfo->get(1)->copy(&zip3Map), new CallExpr(PRIMITIVE_ADD, loop->loopInfo->get(1)->copy(&zip3Map), loop->loopInfo->get(4)->copy(&zip3Map))));
    ii->getZipCursor3->insertAtTail(new CallExpr(PRIMITIVE_SET_MEMBER, zip3Iterator, local2field.get(counter), zip3Map.get(counter)));

    ii->getZipCursor2->insertAtTail(new CallExpr(PRIMITIVE_MOVE, cloopZip2Cond, new CallExpr(PRIMITIVE_LESSOREQUAL, loop->loopInfo->get(1)->copy(&zip2Map), loop->loopInfo->get(3)->copy(&zip2Map))));
    ii->getZipCursor4->insertAtTail(new CallExpr(PRIMITIVE_MOVE, cloopZip4Cond, new CallExpr(PRIMITIVE_LESSOREQUAL, loop->loopInfo->get(1)->copy(&zip4Map), loop->loopInfo->get(3)->copy(&zip4Map))));
  }

  //
  // add BLOCK II to conditional then clause for both getHeadCursor and
  // getNextCursor methods and to the getZipCursor2 method
  //
  BlockStmt* headThen = new BlockStmt();
  BlockStmt* nextThen = new BlockStmt();
  for_alist(expr, loop->body) {
    if (expr == yield)
      break;
    headThen->insertAtTail(expr->copy(&headMap));
    nextThen->insertAtTail(expr->remove());
    ii->getZipCursor2->insertAtTail(expr->copy(&zip2Map));
  }

  //
  // add BLOCK IV to conditional else clause for both getHeadCursor and
  // getNextCursor methods; set cursor to 0
  //
  BlockStmt* headElse = new BlockStmt();
  BlockStmt* nextElse = new BlockStmt();
  loop->remove();
  for_alist(expr, fn->body->body) {
    if (!expr->next) // ignore return statement
      break;
    headElse->insertAtTail(expr->copy(&headMap));
    nextElse->insertAtTail(expr->remove());
    ii->getZipCursor4->insertAtTail(expr->copy(&zip4Map));
  }

  //
  // add conditional to getHeadCursor and getNextCursor methods
  //
  Expr* headCond = loop->loopInfo->get(1)->copy(&headMap);
  Expr* zip1Cond = loop->loopInfo->get(1)->copy(&zip1Map);
  Expr* zip2Cond;
  Expr* zip3Cond = loop->loopInfo->get(1)->copy(&zip3Map);
  Expr* zip4Cond;
  Expr* nextCond = loop->loopInfo->get(1)->remove();
  if (loop->loopInfo->isPrimitive(PRIMITIVE_LOOP_C_FOR)) {
    headCond = new SymExpr(cloopHeadCond);
    nextCond = new SymExpr(cloopNextCond);
    zip2Cond = new SymExpr(cloopZip2Cond);
    zip4Cond = new SymExpr(cloopZip4Cond);
  } else {
    Symbol* tmp;
    tmp = newTemp(ii->getZipCursor2, ii->getZipCursor2->retType);
    ii->getZipCursor2->insertAtTail(new CallExpr(PRIMITIVE_MOVE, tmp, ii->getZipCursor2->getFormal(2)));
    zip2Cond = new SymExpr(tmp);
    tmp = newTemp(ii->getZipCursor4, ii->getZipCursor4->retType);
    ii->getZipCursor4->insertAtTail(new CallExpr(PRIMITIVE_MOVE, tmp, ii->getZipCursor4->getFormal(2)));
    zip4Cond = new SymExpr(tmp);
  }
  ii->getHeadCursor->insertAtTail(new CondStmt(headCond, headThen, headElse));
  ii->getNextCursor->insertAtTail(new CondStmt(nextCond, nextThen, nextElse));

  // load local variables from fields at return points and update
  // fields when local variables change
  forv_Vec(Symbol, local, locals) {
    Symbol* field = local2field.get(local);
    if (toArgSymbol(local)) {
      Symbol* newlocal = newTemp(ii->getNextCursor, local->type, local->name);
      updateOneSymbol(ii->getNextCursor, local, newlocal);

      Symbol* newlocal2;

      newlocal2 = newTemp(ii->getHeadCursor, local->type, local->name);
      updateOneSymbol(ii->getHeadCursor, local, newlocal2);
      headMap.put(newlocal, newlocal2);
      insertGetMember(ii->getHeadCursor, newlocal2, headIterator, field);

      newlocal2 = newTemp(ii->getZipCursor1, local->type, local->name);
      updateOneSymbol(ii->getZipCursor1, local, newlocal2);
      zip1Map.put(newlocal, newlocal2);
      insertGetMember(ii->getZipCursor1, newlocal2, zip1Iterator, field);

      newlocal2 = newTemp(ii->getZipCursor2, local->type, local->name);
      updateOneSymbol(ii->getZipCursor2, local, newlocal2);
      zip2Map.put(newlocal, newlocal2);
      insertGetMember(ii->getZipCursor2, newlocal2, zip2Iterator, field);

      newlocal2 = newTemp(ii->getZipCursor3, local->type, local->name);
      updateOneSymbol(ii->getZipCursor3, local, newlocal2);
      zip3Map.put(newlocal, newlocal2);
      insertGetMember(ii->getZipCursor3, newlocal2, zip3Iterator, field);

      newlocal2 = newTemp(ii->getZipCursor4, local->type, local->name);
      updateOneSymbol(ii->getZipCursor4, local, newlocal2);
      zip4Map.put(newlocal, newlocal2);
      insertGetMember(ii->getZipCursor4, newlocal2, zip4Iterator, field);

      local = newlocal;
    } else {
      if (isRecordType(local->type)) {
        insertGetMember(ii->getHeadCursor, headMap.get(local), headIterator, field);
        insertGetMember(ii->getNextCursor, local, nextIterator, field);
        insertGetMember(ii->getZipCursor1, zip1Map.get(local), zip1Iterator, field);
      }
      insertGetMember(ii->getZipCursor2, zip2Map.get(local), zip2Iterator, field);
      insertGetMember(ii->getZipCursor3, zip3Map.get(local), zip3Iterator, field);
      insertGetMember(ii->getZipCursor4, zip4Map.get(local), zip4Iterator, field);
    }
    insertGetMember(ii->getNextCursor, local, nextIterator, field);
    if (isRecordType(local->type)) {
      insertSetMember(ii->getHeadCursor, headIterator, field, headMap.get(local));
      insertSetMember(ii->getNextCursor, nextIterator, field, local);
      insertSetMember(ii->getZipCursor1, zip1Iterator, field, zip1Map.get(local));
      insertSetMember(ii->getZipCursor2, zip2Iterator, field, zip2Map.get(local));
      insertSetMember(ii->getZipCursor3, zip3Iterator, field, zip3Map.get(local));
      insertSetMember(ii->getZipCursor4, zip4Iterator, field, zip4Map.get(local));
    } else {
      for_defs(se, defMap, local) {
        if (toCallExpr(se->parentExpr))
          insertSetMember(se, nextIterator, field, local);
        SymExpr* se2;
        if ((se2 = toSymExpr(headMap.get(se))))
          if (toCallExpr(se2->parentExpr))
            insertSetMember(se2, headIterator, field, headMap.get(local));
        if ((se2 = toSymExpr(zip1Map.get(se))))
          if (toCallExpr(se2->parentExpr))
            insertSetMember(se2, zip1Iterator, field, zip1Map.get(local));
        if ((se2 = toSymExpr(zip2Map.get(se))))
          if (toCallExpr(se2->parentExpr))
            insertSetMember(se2, zip2Iterator, field, zip2Map.get(local));
        if ((se2 = toSymExpr(zip3Map.get(se))))
          if (toCallExpr(se2->parentExpr))
            insertSetMember(se2, zip3Iterator, field, zip3Map.get(local));
        if ((se2 = toSymExpr(zip4Map.get(se))))
          if (toCallExpr(se2->parentExpr))
            insertSetMember(se2, zip4Iterator, field, zip4Map.get(local));
      }

      // update based on indirect writes via references
      for_uses(se, useMap, local) {
        if (CallExpr* ref = toCallExpr(se->parentExpr))
          if (ref->isPrimitive(PRIMITIVE_SET_REF))
            if (CallExpr* move = toCallExpr(ref->parentExpr))
              if (move->isPrimitive(PRIMITIVE_MOVE))
                if (SymExpr* lhs = toSymExpr(move->get(1)))
                  for_defs(se, defMap, lhs->var) {
                    if (toCallExpr(se->parentExpr))
                      insertSetMember(se, nextIterator, field, local);
                    SymExpr* se2;
                    if ((se2 = toSymExpr(headMap.get(se))))
                      if (toCallExpr(se2->parentExpr))
                        insertSetMember(se2, headIterator, field, headMap.get(local));
                    if ((se2 = toSymExpr(zip1Map.get(se))))
                      if (toCallExpr(se2->parentExpr))
                        insertSetMember(se2, zip1Iterator, field, zip1Map.get(local));
                    if ((se2 = toSymExpr(zip2Map.get(se))))
                      if (toCallExpr(se2->parentExpr))
                        insertSetMember(se2, zip2Iterator, field, zip2Map.get(local));
                    if ((se2 = toSymExpr(zip3Map.get(se))))
                      if (toCallExpr(se2->parentExpr))
                        insertSetMember(se2, zip3Iterator, field, zip3Map.get(local));
                    if ((se2 = toSymExpr(zip4Map.get(se))))
                      if (toCallExpr(se2->parentExpr))
                        insertSetMember(se2, zip4Iterator, field, zip4Map.get(local));
                  }
      }
    }
  }

  ii->getHeadCursor->insertAtTail(new CallExpr(PRIMITIVE_RETURN, headCond->copy()));
  ii->getNextCursor->insertAtTail(new CallExpr(PRIMITIVE_RETURN, nextCond->copy()));

  Symbol* tmp;

  ii->getZipCursor1->insertAtTail(new CallExpr(PRIMITIVE_RETURN, zip1Cond));
  ii->getZipCursor2->insertAtTail(new CallExpr(PRIMITIVE_RETURN, zip2Cond));
  ii->getZipCursor3->insertAtTail(new CallExpr(PRIMITIVE_RETURN, zip3Cond));
  ii->getZipCursor4->insertAtTail(new CallExpr(PRIMITIVE_RETURN, zip4Cond));

  tmp = newTemp(ii->isValidCursor, dtBool);
  ii->isValidCursor->insertAtTail(new CallExpr(PRIMITIVE_MOVE, tmp, ii->isValidCursor->getFormal(2)));
  ii->isValidCursor->insertAtTail(new CallExpr(PRIMITIVE_RETURN, tmp));

  buildGetValue(fn, value);

  ii->getHeadCursor->addPragma("inline");
  ii->getNextCursor->addPragma("inline");
  ii->isValidCursor->addPragma("inline");
  ii->getValue->addPragma("inline");
  ii->getZipCursor1->addPragma("inline");
  ii->getZipCursor2->addPragma("inline");
  ii->getZipCursor3->addPragma("inline");
  ii->getZipCursor4->addPragma("inline");
  fn->addPragma("inline");
}


static void
addLocalVariablesLiveAtYields(Vec<Symbol*>& syms, FnSymbol* fn) {
  buildBasicBlocks(fn);

#ifdef DEBUG_LIVE
  printf("Iterator\n");
  list_view(fn);
#endif

#ifdef DEBUG_LIVE
  printf("Basic Blocks\n");
  printBasicBlocks(fn);
#endif

  Vec<Symbol*> locals;
  Map<Symbol*,int> localMap;
  Vec<SymExpr*> useSet;
  Vec<SymExpr*> defSet;
  Vec<Vec<bool>*> OUT;
  liveVariableAnalysis(fn, locals, localMap, useSet, defSet, OUT);

  int i = 0;
  forv_Vec(BasicBlock, bb, *fn->basicBlocks) {
    bool hasYield = false;
    forv_Vec(Expr, expr, bb->exprs) {
      if (CallExpr* call = toCallExpr(expr))
        if (call->isPrimitive(PRIMITIVE_YIELD))
          hasYield = true;
    }
    if (hasYield) {
      Vec<bool> live;
      for (int j = 0; j < locals.n; j++) {
        live.add(OUT.v[i]->v[j]);
      }
      for (int k = bb->exprs.n - 1; k >= 0; k--) {
        if (CallExpr* call = toCallExpr(bb->exprs.v[k])) {
          if (call->isPrimitive(PRIMITIVE_YIELD)) {
            for (int j = 0; j < locals.n; j++) {
              if (live.v[j]) {
                syms.add_exclusive(locals.v[j]);
              }
            }
          }
        }
        Vec<BaseAST*> asts;
        collect_asts(bb->exprs.v[k], asts);
        forv_Vec(BaseAST, ast, asts) {
          if (SymExpr* se = toSymExpr(ast)) {
            if (defSet.set_in(se)) {
              live.v[localMap.get(se->var)] = false;
            }
            if (useSet.set_in(se)) {
              live.v[localMap.get(se->var)] = true;
            }
          }
        }
      }
    }
    i++;
  }

#ifdef DEBUG_LIVE
  printf("LIVE at Yield Points\n");
  forv_Vec(Symbol, sym, syms) {
    printf("%s[%d]\n", sym->name, sym->id);
  }
  printf("\n");
#endif

  forv_Vec(Vec<bool>, out, OUT)
    delete out;
}


static void
addLocalVariables(Vec<Symbol*>& syms, FnSymbol* fn) {
  buildBasicBlocks(fn);
  forv_Vec(BasicBlock, bb, *fn->basicBlocks) {
    forv_Vec(Expr, expr, bb->exprs) {
      if (DefExpr* def = toDefExpr(expr)) {
        if (VarSymbol* var = toVarSymbol(def->sym))
          if (!var->type->symbol->hasPragma("ref") || var->hasPragma("index var"))
            // do not add references except for indices
            syms.add(var);
      }
    }
  }
}


void lowerIterator(FnSymbol* fn) {
  IteratorInfo* ii = fn->iteratorInfo;

  currentLineno = fn->lineno;
  Vec<BaseAST*> asts;
  collect_asts_postorder(fn, asts);

  Map<Symbol*,Vec<SymExpr*>*> defMap;
  Map<Symbol*,Vec<SymExpr*>*> useMap;
  buildDefUseMaps(fn, defMap, useMap);

  // make fields for all local variables and arguments
  // optimization note: only variables live at yield points are required
  Map<Symbol*,Symbol*> local2field;
  Vec<Symbol*> locals;

  for_formals(formal, fn)
    locals.add(formal);
  if (fNoLiveAnalysis)
    addLocalVariables(locals, fn);
  else
    addLocalVariablesLiveAtYields(locals, fn);
  locals.add_exclusive(fn->getReturnSymbol());

  int i = 0;
  forv_Vec(Symbol, local, locals) {
    Type* type = local->type;
    ClassType* ct = toClassType(type);
    if (isReference(local->type) && local != fn->getReturnSymbol() && !(ct && ct->symbol->hasPragma("_ArrayTypeInfo"))) // && (local->astTag != SYMBOL_ARG || local == fn->_this))
      type = getValueType(local->type);
    Symbol* field =
      new VarSymbol(astr("_", istr(i++), "_", local->name), type);
    local2field.put(local, field);
    ii->classType->fields.insertAtTail(new DefExpr(field));
  }

  Symbol* value = local2field.get(fn->getReturnSymbol());
  INT_ASSERT(value);
  CallExpr* yield = isSingleLoopIterator(fn, asts);
  if (!fNoOptimizeLoopIterators && yield) {
    buildSingleLoopMethods(fn, asts, local2field, locals, value, yield, defMap, useMap);
  } else {
    buildGetNextCursor(fn, asts, local2field, locals, defMap, useMap);
    buildGetHeadCursor(fn);
    buildIsValidCursor(fn);
    buildGetValue(fn, value);
    buildDefaultZipMethods(fn);
  }

  // rebuild iterator function

  for_alist(expr, fn->body->body)
    expr->remove();
  fn->defPoint->remove();
  fn->retType = ii->classType;
  Symbol* t1 = newTemp(fn, ii->classType);
  fn->insertAtTail(new CallExpr(PRIMITIVE_MOVE, t1, new CallExpr(PRIMITIVE_CHPL_ALLOC, ii->classType->symbol, new_StringSymbol("iterator class"))));
  forv_Vec(Symbol, local, locals) {
    Symbol* field = local2field.get(local);
    if (toArgSymbol(local)) {
      insertSetMember(fn, t1, field, local);
    } else if (isRecordType(local->type)) {
      if (field->type->refType) { // skips array types (how to handle arrays?)
        Symbol* tmp = new VarSymbol("_tmp", field->type);
        tmp->isCompilerTemp = true;
        fn->insertAtTail(new DefExpr(tmp));
        insertSetMemberInits(fn, tmp);
        fn->insertAtTail(new CallExpr(PRIMITIVE_SET_MEMBER, t1, field, tmp));
      }
    } else if (field->type->symbol->hasPragma("ref")) {
      // do not initialize references
    } else if (field->type->defaultValue) {
      insertSetMember(fn, t1, field, field->type->defaultValue);
    }
  }
  fn->insertAtTail(new CallExpr(PRIMITIVE_RETURN, t1));
  ii->getValue->defPoint->insertAfter(new DefExpr(fn));

  freeDefUseMaps(defMap, useMap);
}
