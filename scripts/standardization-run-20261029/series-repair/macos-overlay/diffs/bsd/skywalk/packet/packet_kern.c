--- ubuntu/bsd/skywalk/packet/packet_kern.c
+++ mac/bsd/skywalk/packet/packet_kern.c
@@ -285,6 +285,36 @@
     uint16_t *val, const boolean_t tx)
 {
 	return __packet_get_inet_checksum(ph, start, val, tx);
+}
+
+errno_t
+kern_packet_set_fpd_command(const kern_packet_t ph,
+							uint8_t cmd)
+{
+	errno_t result;
+
+	if (cmd > 7)
+		return 22;
+	result = 0;
+	PKT_ADDR(ph)->pkt_fpd_metadata |= ((cmd & 7) << 6) | 0x8000;
+	return result;
+}
+
+errno_t
+kern_packet_set_fpd_sequence_number(const kern_packet_t ph,
+									uint32_t seq_num)
+{
+	PKT_ADDR(ph)->pkt_fpd_seqnum = seq_num;
+	PKT_ADDR(ph)->pkt_fpd_metadata |= 0x8000;
+	return 0;
+}
+
+errno_t
+kern_packet_set_fpd_context_id(const kern_packet_t ph,
+							   uint16_t ctx_id)
+{
+	PKT_ADDR(ph)->pkt_fpd_metadata |= ctx_id & 0x3F | 0x8000;
+	return 0;
 }
 
 void
