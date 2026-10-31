--- ubuntu/osfmk/arm64/start.s
+++ mac/osfmk/arm64/start.s
@@ -756,7 +756,7 @@
 	dsb		ish
 
 
-#if defined(BCM2837)
+#if defined(BCM2837) && (!defined(XNU_NATIVE_ARCH_TIMER) || !XNU_NATIVE_ARCH_TIMER)
 	// Setup timer interrupt routing; must be done before MMU is enabled
 	mrs		x15, MPIDR_EL1						// Load MPIDR to get CPU number
 	and		x15, x15, #0xFF						// CPU number is in MPIDR Affinity Level 0
