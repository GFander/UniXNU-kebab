--- ubuntu/bsd/skywalk/packet/packet_var.h
+++ mac/bsd/skywalk/packet/packet_var.h
@@ -474,7 +474,9 @@
 
 	void *      pkt_priv;   /* free to use for every layer */
 
-
+	uint32_t               pkt_fpd_seqnum;   // @ 0xd0
+	uint16_t               pkt_fpd_metadata; // @ 0xd4
+	
 	/*
 	 * Kernel specific.
 	 *
