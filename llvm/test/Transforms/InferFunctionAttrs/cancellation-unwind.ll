; RUN: opt -mtriple=x86_64-unknown-linux-gnu -passes=inferattrs,simplifycfg -S %s | FileCheck %s --check-prefixes=CHECK,LINUX
; RUN: opt -mtriple=mmix-unknown-linux -passes=inferattrs,simplifycfg -S %s | FileCheck %s

; Possible pthread cancellation points must preserve unwind cleanup. Other
; inferred attributes and explicit nonthrowing declarations remain intact.
; CHECK: declare noalias noundef ptr @fopen({{.*}}) #[[CANCEL:[0-9]+]]
; CHECK: declare noalias noundef ptr @opendir({{.*}}) #[[CANCEL]]
; CHECK: declare noundef i32 @fclose({{.*}}) #[[CANCEL]]
; CHECK: declare noundef i32 @closedir({{.*}}) #[[CANCEL]]
; CHECK: declare noundef i32 @fileno({{.*}}) #[[NOTHROW:[0-9]+]]
; CHECK: declare noundef i32 @puts({{.*}}) #[[NOTHROW]]
declare ptr @fopen(ptr, ptr)
declare ptr @opendir(ptr)
declare i32 @fclose(ptr)
declare i32 @closedir(ptr)
declare i32 @fileno(ptr)
declare i32 @puts(ptr) nounwind
; LINUX: declare noundef i32 @_IO_getc({{.*}}) #[[CANCEL]]
; LINUX: declare noundef i32 @_IO_putc({{.*}}) #[[CANCEL]]
; LINUX: declare noundef i32 @__isoc99_scanf({{.*}}) #[[CANCEL]]
declare i32 @_IO_getc(ptr)
declare i32 @_IO_putc(i32, ptr)
declare i32 @__isoc99_scanf(ptr, ...)
declare i32 @__gxx_personality_v0(...)
declare void @cleanup()

; CHECK-LABEL: define void @resource_cleanup(
; CHECK: invoke ptr @fopen(
; CHECK: invoke ptr @opendir(
; CHECK: invoke i32 @fclose(
; CHECK: invoke i32 @closedir(
; CHECK: landingpad
; CHECK: call void @cleanup()
; CHECK: resume
define void @resource_cleanup(ptr %path, ptr %mode)
    personality ptr @__gxx_personality_v0 {
entry:
  %file = invoke ptr @fopen(ptr %path, ptr %mode)
      to label %directory unwind label %failure
directory:
  %dir = invoke ptr @opendir(ptr %path)
      to label %close_file unwind label %failure
close_file:
  %f = invoke i32 @fclose(ptr %file)
      to label %close_dir unwind label %failure
close_dir:
  %d = invoke i32 @closedir(ptr %dir)
      to label %done unwind label %failure
done:
  ret void
failure:
  %exception = landingpad { ptr, i32 } cleanup
  call void @cleanup()
  resume { ptr, i32 } %exception
}

; CHECK-DAG: attributes #[[CANCEL]] = { nofree }
; CHECK-DAG: attributes #[[NOTHROW]] = { nofree nounwind }
