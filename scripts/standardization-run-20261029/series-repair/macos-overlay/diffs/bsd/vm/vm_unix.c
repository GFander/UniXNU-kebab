--- ubuntu/bsd/vm/vm_unix.c
+++ mac/bsd/vm/vm_unix.c
@@ -3707,6 +3707,9 @@
 		}
 	}
 	break;
+	case KAS_INFO_KERNEL_SPECIAL_SEGMENT_1:
+	case KAS_INFO_KERNEL_SPECIAL_SEGMENT_2:
+	break;
 	default:
 		return EINVAL;
 	}
