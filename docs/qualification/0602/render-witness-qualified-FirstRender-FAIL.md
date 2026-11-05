## 0602同页CP写入诊断完成：FirstRender=FAIL

原安全304.405秒和原GPU/CP/后缀回归通过。只有一次GPURENDER，三份CAP38一致SHA-256 0c3d1c9809a53c166f05f6ffe5c35e0163b46f0b4806b784280d47aecd9ba539。第一目标同页CP控制可见=True；实际图像摘要及逐像素状态：[{"pass_index": 0, "submitted": true, "captured": true, "BO_sha256": "c698b0cead6b55a34759a3a06a19e24b21a81629626f984175c353d5d2b6cc39", "same_page_CP_control": true, "observed_control_word": "0xa650c001", "image_bytes": 4096, "image_sha256": "f600eca824e84a43f0691b267bd620e462c50da165c5b80e17aecb7a924f1fa8", "all_pixels_A5": true, "terminal": {"wptr": 159, "rptr": 159, "shadow": 159, "fence": 3, "marker": 2790263821, "status": 1, "rbbm_irq": 0, "irq_mask": 0, "cp_fault": 0, "cp_irq": 0, "cp_protect": 0, "ahb_fence": 0, "gfsr": 0, "fsr": 1024, "sqe": 1, "timestamps": [3968711036, 3968711041, 3968711150]}, "first_mismatch": 256}, {"pass_index": 1, "submitted": false, "captured": false, "same_page_CP_control": null}]。CP写入标记不是绘制像素，不扩大为RB/CCU执行成功。原Fault/超时/隔离保留，不重试writer、不清现场。人工602状态见独立收据（若未存在则NOT_RUN），亮度暂缓。

严格记录native-dpu-0602/0602-render-witness-physical-qualified.json SHA-256 d57503fb051bdf26ac94289e054f0ab9b64d4b8ffd76b6288cc198673d84d1c8，90个raw样本hash重核；native 994662ac7139addc53e22fc155824ff40e694893cd010da8b2b3ae476776644c，boot 0ce91cd4e338954af75df265cbebb460dbf8f7c8ee73686b99d56e17a0f8af9c，456+4/5456，Git d5c9c62db752f225a288b88959d3754edc94138a。所有原始CAP38、wire/log、BO和截取像素均保存，0591历史不改。PAS13重复支持查询路线已关闭，本轮未查询，ZAP实际认证NOT_RUN/UNKNOWN。

下一根据同页CP/像素结果聚焦2D状态。固定Mesa A650源码明确初始restore写RB_DBG_ECO_CNTL=0x04100000；normal与blit相等只跳过切换而不省初始写，当前遗漏已审计但根因未证实。可聚焦该单一初始化改动再完整安全实机验证；不得无证据转回ZAP前置或盲写TRUST。

