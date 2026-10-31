
#include <cassert>
#include <cstdio>
#include <cstring>
#include <deque>
#include <vector>
#include "kebab_tcm_wire.h"
static std::deque<std::vector<uint8_t>> frames;
static unsigned writes,reads,writeFail;static bool wrote;
static struct{bool tcm_seen;}kt;
template<typename...T>static void trace(const char *,T...){}
#define printf(...) trace(__VA_ARGS__)
#define kebab_geni_trace(...) trace(__VA_ARGS__)
static void delay(unsigned){}
static void kt_hexdump(const char *,const uint8_t *,unsigned){}
static const char *kt_status_name(uint8_t){return "actual status";}
static int kebab_geni_i2c_write(const uint8_t *p,unsigned n){assert(n>=2&&n<=64&&p);writes++;wrote=true;return writes==writeFail?-1:0;}
static int kebab_geni_i2c_read(uint8_t *out,unsigned n){
 assert(wrote);reads++;if(frames.empty())return -1;
 auto f=frames.front();frames.pop_front();assert(n==f.size());memcpy(out,f.data(),n);return 0;
}
static int
kt_tcm_wire_write(void *context, const uint8_t *bytes, uint32_t length)
{
    (void)context;
    return kebab_geni_i2c_write(bytes, length);
}

static int
kt_tcm_wire_read(void *context, uint8_t *bytes, uint32_t length)
{
    (void)context;
    return kebab_geni_i2c_read(bytes, length);
}

static int
kt_tcm_read_header(uint8_t cmd, kebab_tcm_header *header)
{
    uint8_t bytes[KEBAB_TCM_HEADER_SIZE];
    if (kebab_geni_i2c_read(bytes, sizeof(bytes))) return -1;
    kebab_tcm_status status = kebab_tcm_header_parse(bytes, header);
    if (status != KEBAB_TCM_OK || header->code == 0x03) {
        printf("KEBAB TOUCH: command 0x%02x invalid transaction header %02x %02x %02x %02x; no byte sliding\n",
            cmd, bytes[0], bytes[1], bytes[2], bytes[3]);
        return -1;
    }
    return 0;
}

static int
kt_tcm_command(uint8_t cmd, const uint8_t *payload, uint32_t len,
    uint8_t *resp, uint32_t resp_size, uint32_t *resp_len, uint8_t *resp_code,
    uint8_t want_report)
{
	uint8_t out[KEBAB_TCM_REPORT_CONFIG_MAX + 3];
	uint8_t scratch[KEBAB_TCM_MAX_PAYLOAD + 1];
	kebab_tcm_header header;
	uint32_t written;
	uint32_t i;
	int err;

	written = kebab_tcm_build_command(out, sizeof(out), cmd, payload, len);
	if (written == 0) {
		printf("KEBAB TOUCH: command 0x%02x: a %u-byte payload does not fit the write buffer\n",
		    cmd, len);
		return -1;
	}
	kebab_geni_trace("KEBAB TOUCH: TCM command 0x%02x, %u payload byte(s)\n", cmd,
	    len);
	err = kebab_tcm_wire_command(out, written, kt_tcm_wire_write, NULL);
	if (err != 0) {
		printf("KEBAB TOUCH: command 0x%02x was not written\n", cmd);
		return -1;
	}
	kebab_geni_trace("KEBAB TOUCH: TCM command 0x%02x written, waiting for header\n",
	    cmd);

	/*
	 * A chip that never answers must not hold the single CPU forever: the
	 * old 100 x 1 ms retry loop can spend more than a second per command,
	 * and each bit-bang read can itself burn a bounded millisecond per
	 * stuck bit.  20 attempts is enough for a healthy part (it answers on
	 * the first) and keeps a dead bus bounded.
	 */
	for (i = 0; i < 20; i++) {
		kebab_tcm_status st;

		if (kt_tcm_read_header(cmd, &header) != 0) {
			if (i < 4) {
				kebab_geni_trace("KEBAB TOUCH: no usable TCM header yet, attempt %u\n",
				    i + 1);
			}
			delay(1000);
			continue;
		}
        /* IDLE has no continuation body. Never consume the next a5 marker
         * as a one-byte padding read. BUSY likewise has no useful payload. */
        if (header.code == 0x00 || header.code == 0x02) {
            delay(1000);continue;
        }
        err = kebab_tcm_wire_body(header.payload_len, scratch, sizeof(scratch),
            kt_tcm_wire_read, NULL);
        if (err) {
            printf("KEBAB TOUCH: command 0x%02x invalid continued body; not decoded\n", cmd);
            return -1;
        }
        printf("KEBAB TOUCH: command 0x%02x answer: code 0x%02x, %u payload byte(s), TCM framing validated\n",
            cmd, header.code, header.payload_len);
        if (header.payload_len) kt_hexdump("answer", scratch, header.payload_len);

		if (want_report != 0) {
			/*
			 * A status where a report was expected is the chip
			 * refusing the command, not a message to skip: identify
			 * needs the report to carry the mode.
			 */
			if (header.code < KEBAB_TCM_REPORT_IDENTIFY) {
				printf("KEBAB TOUCH: command 0x%02x was answered with status 0x%02x (%s), not with report 0x%02x\n",
				    cmd, header.code, kt_status_name(header.code),
				    want_report);
				return -1;
			}
			st = kebab_tcm_expect_report(&header, want_report);
		} else {
			if (header.code >= KEBAB_TCM_REPORT_IDENTIFY) {
				printf("KEBAB TOUCH: report 0x%02x arrived while waiting for command 0x%02x; read out\n",
				    header.code, cmd);
				delay(1000);
				continue;
			}
			st = kebab_tcm_expect_status(&header);
		}
		if (st != KEBAB_TCM_OK) {
			printf("KEBAB TOUCH: command 0x%02x: code 0x%02x is not the answer (%s); read out and waiting\n",
			    cmd, header.code, kebab_tcm_status_name(st));
			delay(1000);
			continue;
		}

		kt.tcm_seen = true;
		*resp_code = header.code;
		{
			/*
			 * The whole payload was read even when the caller's
			 * buffer is smaller, so a truncating decode cannot leave
			 * bytes behind either.
			 */
			uint32_t copy = header.payload_len;

			if (copy > resp_size) {
				printf("KEBAB TOUCH: command 0x%02x: payload %u does not fit %u, truncating\n",
				    cmd, header.payload_len, resp_size);
				copy = resp_size;
			}
			if (copy != 0 && resp != NULL) {
				memcpy(resp, scratch, copy);
			}
			if (resp_len != NULL) {
				*resp_len = copy;
			}
		}
		return 0;
	}
	printf("KEBAB TOUCH: command 0x%02x: no usable answer after %u attempts\n",
	    cmd, i);
	return -1;
}

static void reset(){frames.clear();writes=reads=writeFail=0;wrote=false;kt.tcm_seen=false;}
static void header(unsigned code,unsigned n){frames.push_back({0xa5,uint8_t(code),uint8_t(n),uint8_t(n>>8)});}
static void body(std::vector<uint8_t> p){p.insert(p.begin(),{0xa5,3});p.push_back(0x5a);frames.push_back(p);}
int main(){
 uint8_t out[128];unsigned len=999;uint8_t code=0xee;
 reset();header(0,0);header(0x1d,2);body({0x11,0x22});header(1,4);body({1,0,0,0});
 assert(!kt_tcm_command(0x20,nullptr,0,out,128,&len,&code,0));
 assert(writes==1&&reads==5&&frames.empty()&&kt.tcm_seen&&len==4&&code==1&&out[0]==1&&out[1]==0);
 reset();header(1,0);len=999;code=0xee;
 assert(!kt_tcm_command(0x26,nullptr,0,out,128,&len,&code,0)&&reads==1&&len==0&&code==1);
 reset();header(1,2);frames.push_back({0xa5,2,8,9,0x5a});len=999;code=0xee;
 assert(kt_tcm_command(0x20,nullptr,0,out,128,&len,&code,0)!=0&&!kt.tcm_seen&&len==999&&code==0xee);
 reset();frames.push_back({0xa4,0,0,0});header(1,1);body({0x45});
 assert(!kt_tcm_command(0x20,nullptr,0,out,128,&len,&code,0)&&reads==3&&len==1&&out[0]==0x45);
 reset();for(unsigned i=0;i<20;i++)header(0,0);len=999;code=0xee;
 assert(kt_tcm_command(0x20,nullptr,0,out,128,&len,&code,0)!=0&&reads==20&&writes==1&&!kt.tcm_seen&&len==999&&code==0xee);
reset();header(1,0);uint8_t config[128]={};
 assert(!kt_tcm_command(0x26,config,128,out,128,&len,&code,0)&&writes==3&&reads==1&&frames.empty());
 reset();writeFail=2;header(1,0);len=999;code=0xee;
 assert(kt_tcm_command(0x26,config,128,out,128,&len,&code,0)!=0&&writes==2&&reads==0&&frames.size()==1&&!kt.tcm_seen&&len==999&&code==0xee);
 std::puts("PASS actual command/header bodies: IDLE no body read, interleaved report, prefix refusal, complete-header retry and bounded unanswered command; I2C explicit transcript");
}
