#if defined(KEBAB)
static void
kebab_fairplay_match_evidence(unsigned kind,OSDictionary *dict,int result,unsigned present)
{
 if(!kebab_fairplay_resource_observer_enabled())return;
 OSString *query=dict?OSDynamicCast(OSString,dict->getObject("IOProviderClass")):NULL;
 if(!query&&dict)query=OSDynamicCast(OSString,dict->getObject("IONameMatch"));
 kebab_fairplay_service_evidence(kind,query?query->getCStringNoCopy():"no-string-class",128,result,present!=0);
}
#endif

