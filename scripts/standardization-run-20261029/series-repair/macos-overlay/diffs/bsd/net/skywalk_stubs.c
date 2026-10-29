--- ubuntu/bsd/net/skywalk_stubs.c
+++ mac/bsd/net/skywalk_stubs.c
@@ -135,6 +135,9 @@
 STUB(kern_packet_get_transport_traffic_background)
 STUB(kern_packet_get_transport_traffic_realtime)
 STUB(kern_packet_set_compression_generation_count);
+STUB(kern_packet_set_fpd_command);
+STUB(kern_packet_set_fpd_context_id);
+STUB(kern_packet_set_fpd_sequence_number);
 STUB(kern_packet_set_flow_uuid);
 STUB(kern_packet_set_inet_checksum);
 STUB(kern_packet_set_headroom);
