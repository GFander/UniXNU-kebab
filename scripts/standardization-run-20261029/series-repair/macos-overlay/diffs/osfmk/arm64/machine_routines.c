--- ubuntu/osfmk/arm64/machine_routines.c
+++ mac/osfmk/arm64/machine_routines.c
@@ -2142,7 +2142,7 @@
 nonspeculative_timebase(void)
 {
 #if defined(HAS_ACNTVCT)
-	return __builtin_arm_rsr64("ACNTVCT_EL0");
+	return __builtin_arm_rsr64("S3_4_c15_c10_6");
 #elif __ARM_ARCH_8_6__
 	return __builtin_arm_rsr64("CNTVCTSS_EL0");
 #else
