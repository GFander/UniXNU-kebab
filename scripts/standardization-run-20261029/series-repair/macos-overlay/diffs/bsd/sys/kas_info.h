--- ubuntu/bsd/sys/kas_info.h
+++ mac/bsd/sys/kas_info.h
@@ -44,7 +44,12 @@
 #define KAS_INFO_KERNEL_TEXT_SLIDE_SELECTOR     (0) /* returns uint64_t */
 #define KAS_INFO_KERNEL_SEGMENT_VMADDR_SELECTOR (1)
 
-#define KAS_INFO_MAX_SELECTOR                   (2)
+// CoreSymbolication`_createSymbolicatorWithMachKernelExcludingSearchPathsFlagsAndNotification
+// expects kas_info to not return an error for these two segments; required for DTrace to work.
+#define KAS_INFO_KERNEL_SPECIAL_SEGMENT_1       (2)
+#define KAS_INFO_KERNEL_SPECIAL_SEGMENT_2       (3)
+
+#define KAS_INFO_MAX_SELECTOR                   (4)
 
 #ifndef KERNEL
 
