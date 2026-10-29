--- ubuntu/makedefs/MakeInc.cmd
+++ mac/makedefs/MakeInc.cmd
@@ -187,10 +187,10 @@
 	export CXX := $(shell $(XCRUN) -sdk $(SDKROOT) -find clang++)
 endif
 ifeq ($(origin MIG),undefined)
-	export MIG := $(shell $(XCRUN) -sdk $(SDKROOT) -find mig)
+	export MIG := $(shell find $(FAKEROOT_DIR) -name "mig")
 endif
 ifeq ($(origin MIGCOM),undefined)
-	export MIGCOM := $(shell $(XCRUN) -sdk $(SDKROOT) -find migcom)
+	export MIGCOM := $(shell find $(FAKEROOT_DIR) -name "migcom")
 endif
 ifeq ($(origin MIGCC),undefined)
 	export MIGCC := $(CC)
