--- ubuntu/bsd/skywalk/packet/os_packet.h
+++ mac/bsd/skywalk/packet/os_packet.h
@@ -875,6 +875,13 @@
     kern_buflet_t pbuf);
 extern void kern_pbufpool_destroy(kern_pbufpool_t);
 extern kern_segment_idx_t kern_segment_get_index(const kern_segment_t);
+
+/*
+ * FPD.
+ */
+extern errno_t kern_packet_set_fpd_command(const kern_packet_t, uint8_t);
+extern errno_t kern_packet_set_fpd_context_id(const kern_packet_t, uint16_t);
+extern errno_t kern_packet_set_fpd_sequence_number(const kern_packet_t, uint32_t);
 __END_DECLS
 #endif /* KERNEL */
 #endif /* PRIVATE */
