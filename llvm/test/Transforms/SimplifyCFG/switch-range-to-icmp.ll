; NOTE: Assertions have been autogenerated by utils/update_test_checks.py
; RUN: opt %s -simplifycfg -S | FileCheck %s

declare i32 @f(i32)

define i32 @basic(i32 %x) {
; CHECK-LABEL: @basic(
; CHECK-NEXT:  entry:
; CHECK-NEXT:    [[X_OFF:%.*]] = add i32 [[X:%.*]], -5
; CHECK-NEXT:    [[SWITCH:%.*]] = icmp ult i32 [[X_OFF]], 3
; CHECK-NEXT:    br i1 [[SWITCH]], label [[A:%.*]], label [[DEFAULT:%.*]]
; CHECK:       default:
; CHECK-NEXT:    [[TMP0:%.*]] = call i32 @f(i32 0)
; CHECK-NEXT:    ret i32 [[TMP0]]
; CHECK:       a:
; CHECK-NEXT:    [[TMP1:%.*]] = call i32 @f(i32 1)
; CHECK-NEXT:    ret i32 [[TMP1]]
;

entry:
  switch i32 %x, label %default [
  i32 5, label %a
  i32 6, label %a
  i32 7, label %a
  ]
default:
  %0 = call i32 @f(i32 0)
  ret i32 %0
a:
  %1 = call i32 @f(i32 1)
  ret i32 %1
}


define i32 @unreachable(i32 %x) {
; CHECK-LABEL: @unreachable(
; CHECK-NEXT:  entry:
; CHECK-NEXT:    [[X_OFF:%.*]] = add i32 [[X:%.*]], -5
; CHECK-NEXT:    [[SWITCH:%.*]] = icmp ult i32 [[X_OFF]], 3
; CHECK-NEXT:    br i1 [[SWITCH]], label [[A:%.*]], label [[B:%.*]]
; CHECK:       a:
; CHECK-NEXT:    [[TMP0:%.*]] = call i32 @f(i32 0)
; CHECK-NEXT:    ret i32 [[TMP0]]
; CHECK:       b:
; CHECK-NEXT:    [[TMP1:%.*]] = call i32 @f(i32 1)
; CHECK-NEXT:    ret i32 [[TMP1]]
;

entry:
  switch i32 %x, label %unreachable [
  i32 5, label %a
  i32 6, label %a
  i32 7, label %a
  i32 10, label %b
  i32 20, label %b
  i32 30, label %b
  i32 40, label %b
  ]
unreachable:
  unreachable
a:
  %0 = call i32 @f(i32 0)
  ret i32 %0
b:
  %1 = call i32 @f(i32 1)
  ret i32 %1
}


define i32 @unreachable2(i32 %x) {
; CHECK-LABEL: @unreachable2(
; CHECK-NEXT:  entry:
; CHECK-NEXT:    [[X_OFF:%.*]] = add i32 [[X:%.*]], -5
; CHECK-NEXT:    [[SWITCH:%.*]] = icmp ult i32 [[X_OFF]], 3
; CHECK-NEXT:    br i1 [[SWITCH]], label [[A:%.*]], label [[B:%.*]]
; CHECK:       a:
; CHECK-NEXT:    [[TMP0:%.*]] = call i32 @f(i32 0)
; CHECK-NEXT:    ret i32 [[TMP0]]
; CHECK:       b:
; CHECK-NEXT:    [[TMP1:%.*]] = call i32 @f(i32 1)
; CHECK-NEXT:    ret i32 [[TMP1]]
;

entry:
  ; Note: folding the most popular case destination into the default
  ; would prevent switch-to-icmp here.
  switch i32 %x, label %unreachable [
  i32 5, label %a
  i32 6, label %a
  i32 7, label %a
  i32 10, label %b
  i32 20, label %b
  ]
unreachable:
  unreachable
a:
  %0 = call i32 @f(i32 0)
  ret i32 %0
b:
  %1 = call i32 @f(i32 1)
  ret i32 %1
}
