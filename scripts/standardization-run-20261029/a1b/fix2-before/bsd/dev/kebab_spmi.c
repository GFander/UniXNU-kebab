/* Boot orchestration only. The arbiter owns mappings and transactions;
 * PON/PMIC keys/rail-read clients own device policy. RPMh has its own RSC
 * transport and the original tested TCS core. Legacy boot arguments remain.
 */
#if defined(KEBAB)
#include "kebab_spmi_bus.h"
#include "kebab_spmi_clients.h"
void
kebab_spmi_init(void)
{
	static bool attempted;
	if (attempted) { return; }
	attempted = true;
	if (!kebab_spmi_bus_init()) { return; }
	kebab_spmi_vrm_init();
	kebab_spmi_pon_prepare();
}
#endif /* KEBAB */
