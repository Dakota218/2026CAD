# Codex 修改紀錄

此檔案用來記錄 Codex 從現在開始協助修改程式的內容。

## 2026-06-16

- 在 `optimizer.cpp` 加入 optimizer 內部時間保護。
  - 使用 `std::chrono::steady_clock` 計時。
  - 當時間限制到達時停止最佳化，並回傳目前找到的最佳 clock tree。
  - 目前最佳化時間限制為 540 秒。
- 在 `optimizer.cpp` 加入 candidate 數量上限。
  - Phase 1 candidate 上限為 512。
  - Phase 2 candidate 上限為 512。
  - 避免大型 testcase 花太久逐一評估所有可能 candidate。
- 修正 `optimizer.cpp` 中 Phase 2 的走訪安全性。
  - 先收集 node name，再修改 `best_tree`。
  - 避免更新最佳解時造成 reference 或 iterator 失效。
- 在 `main.cpp` 進入 optimizer 前加入 `std::cout.flush()`。
  - 讓 Linux 執行時的 log 更清楚顯示已經開始最佳化。
- 已確認專案可用 `make` 成功編譯。

## 2026-06-16 後續最佳化

- 修正 `timing_engine.cpp` 的 slack 公式。
  - SS setup slack 改為 `Tclk - 0.08 * Tclk - data_delay + skew`。
  - FF hold slack 改為 `data_delay - 0.05 * Tclk - skew`。
  - TNS/WNS 仍只累加負 slack。
- 在 `main.cpp` 檢查 SS/FF clock period 是否一致。
  - 若不一致會印出 warning，並繼續使用 SS clock period 作為 contest Tclk。
- 重寫 `optimizer.cpp` 的 greedy optimizer。
  - 參考 pasted text 中整理的兩篇論文方向，不依賴 PDF 抽取。
  - 使用 setup/hold 負 slack 建立 endpoint 與 ancestor criticality。
  - 對 setup-critical capture 端增加延遲優先權。
  - 對 hold-critical launch 端增加延遲優先權，並降低 hold-critical capture 端加延遲的優先權。
  - 每輪產生 leaf buffer insertion、existing buffer resizing、部分 internal insertion 候選。
  - leaf/internal insertion 會嘗試 `buf.lib` 中所有 buffer cell type，不再只用最小 buffer。
  - resize 會嘗試所有合法 cell type，不再只改成最大 buffer。
  - 候選先用估計分數排序，再精確呼叫 `TimingEngine` 評估。
  - 採用 repeated greedy：每輪套用最佳改善 move，最多 8 輪。
- 調整 candidate 限制。
  - `kMaxCandidatesPerIteration` 改為 50000。
  - `kMaxExactEvaluationsPerIteration` 設為 1024，避免每輪精確評估過多導致超時。
  - 原本的 `kMaxPhase1Candidates = 512` 與 `kMaxPhase2Candidates = 512` 已移除。
- 加入 optimizer debug output。
  - 印出初始 score、每輪產生/評估 candidate 數、每輪最佳 score、elapsed time、最終 metrics、inserted buffer 數與 resize move 數。
- 已重新 `make` 編譯成功。

## 2026-06-17 修正 Candidate C++11 初始化錯誤

- 修正 `optimizer.cpp` 在 `-std=c++11` 下無法編譯的問題。
  - 錯誤原因是 `Candidate` struct 含有預設成員初始化，C++11 下無法用 `{...}` initializer list 直接 `push_back` 或建構。
  - 在 `Candidate` 中加入明確的 default constructor 與 parameterized constructor。
  - 保留原本 `candidates.push_back({...})` 與 `Candidate c{...}` 的寫法可正常使用。
- 已重新 `make` 編譯成功。
  - 僅剩 `collectSinksDfs` 未使用的 warning，不影響連結與執行。
- 使用 testcase2 做 sanity run。
  - 約 14 秒完成。
  - 成功產生 `testcase2/modified_clk_tree.structure`。

## 2026-06-16 WNS 優先 greedy 改版

- 依照使用者列出的 9 點需求重寫 `optimizer.cpp` 的候選產生與評分方式。
- 加入 top violated path 分析與輸出。
  - 初始狀態會印出 top 50 SS setup violated paths。
  - 初始狀態會印出 top 20 FF hold violated paths。
  - 每筆會列出 slack、launch FF、capture FF、data delay 與 skew。
- 每輪改成根據當前 top violated paths 產生 candidate。
  - SS setup violation 優先對 capture FF 加 clock delay。
  - FF hold violation 優先對 launch FF 加 clock delay。
  - 會把 priority 傳到部分 ancestor，用於 resize 與 internal insertion candidate。
- leaf insertion 改為嘗試 `buf.lib` 中所有 buffer type。
- leaf insertion 支援 2-buffer chain。
  - 允許 `parent -> NEW_BUF_A -> NEW_BUF_B -> sink`。
  - 兩顆 buffer 各自嘗試所有合法 cell type 組合。
- resize 保持嘗試所有合法 cell type。
  - 只跳過 fanout 超過該 cell max fanout 的非法選項。
- repeated greedy 改成沒有固定 iteration 上限。
  - 持續執行直到 540 秒時間限制、沒有合法 candidate，或連續數輪沒有改善。
- scoring 改成 WNS 優先。
  - 先比較 SS/FF 中最差 WNS。
  - 再比較 SS WNS + FF WNS。
  - 再比較 SS/FF TNS 總和。
  - 再比較 SS/FF NVP 總數。
  - 最後才比較 area。
- candidate 數量設定：
  - 每輪最多保留 50000 個排序後 candidate。
  - 每輪最多精確評估 4096 個 candidate。
- 已重新 `make` 編譯成功。
- 使用 testcase2 做 90 秒上限 sanity run。
  - 測試被外層 alarm 中止，未等待完整輸出。
  - 觀察到 SS WNS 從 -0.578 改善到約 -0.4818。
  - 觀察到 FF hold violation 已從 4 個降到 0 個。

## 2026-06-16 WNS Repair Chain Candidate

- 實作 WNS repair phase 的第一版。
  - 針對每輪 top 10 SS setup violated paths 的 capture FF 產生專門 candidate。
  - 使用 `need_delay = -slack` 估算需要補的 clock delay。
  - 依照 chain 的 SS delay 是否接近 `need_delay` 來排序 candidate。
- leaf insertion chain 支援從 2-buffer 擴充到 3-buffer。
  - 一般 candidate 仍維持 1-buffer 與 2-buffer chain。
  - top 10 SS WNS repair candidate 會額外嘗試 3-buffer chain。
  - 3-buffer chain 格式為 `parent -> NEW_BUF_A -> NEW_BUF_B -> NEW_BUF_C -> sink`。
- 更新 `applyMove()`。
  - 改成可套用 1 到 3 顆 buffer 的 chain。
  - 仍維持新 buffer 使用 `NEW_BUF_X` 且名稱唯一。
  - 保留原本 parent-child 順序，只在原 parent-child edge 中間插入新 chain。
- 已重新 `make` 編譯成功。
- 使用 testcase2 做 90 秒上限 sanity run。
  - 測試被外層 alarm 中止，未等待完整輸出。
  - 3-buffer chain candidate 產生與套用流程正常。
  - 觀察到候選數約 12k 到 13k，沒有超過每輪 50000 上限。
  - 觀察到 SS WNS 從 -0.578 改善到約 -0.478。

## 2026-06-17 TNS/NVP Cleanup Candidate

- 實作 TNS/NVP cleanup phase 的第一版。
  - 每輪額外分析 top 1000 SS setup violated paths。
  - 依 capture FF 聚合 violated path 數量、累積 negative slack、最差 slack。
  - 取前 200 個高 priority capture FF 作為 cleanup focus。
- cleanup priority 設計：
  - `count` 越多代表同一個 capture FF 影響越多 violated paths。
  - `total_neg_slack` 越大代表對 TNS 貢獻越大。
  - `worst_slack` 越差代表也可能幫助 WNS 周邊路徑。
- 將 cleanup focus 接到原本 candidate generator。
  - 對高 priority capture FF 產生 leaf insertion candidate。
  - 對其 ancestor 傳遞 priority，產生 resize 與 internal insertion candidate。
  - 目標是讓 candidate 不只修最差 WNS，也能找到一次改善多條 path 的 move。
- 調整每輪精確評估數。
  - `kMaxExactEvaluationsPerIteration` 從 4096 提高到 8192。
  - 原因是 WNS repair 的 3-buffer chain candidate 約會佔 3930 個名額，若維持 4096，TNS cleanup candidate 幾乎排不到精確評估。
- 已重新 `make` 編譯成功。
- 使用 testcase2 做 90 秒上限 sanity run。
  - 測試被外層 alarm 中止，未等待完整輸出。
  - 候選數約 45k 到 46k，仍低於每輪 50000 上限。
  - 每輪 8192 個 exact evaluation 約需 18 秒。
  - 前幾輪仍以 WNS-first move 為主，預期 TNS/NVP cleanup 會在 WNS improvement 較難繼續推進後開始發揮。

## 2026-06-17 Robust Score 與接受條件改版

- 依照使用者提供的官方 score 公式重寫 optimizer 評分。
  - `setup_score = (1 - TNS_SS / TNS_SS_ori) + (1 - WNS_SS / WNS_SS_ori)`。
  - `hold_score = (1 - TNS_FF / TNS_FF_ori) + (1 - WNS_FF / WNS_FF_ori)`。
  - `area_score = 1 - Area / Area_ori`。
- 實作未知 alpha/beta/gamma 的 robust score。
  - 測試 alpha = 0.36, 0.40, 0.50, 0.60, 0.70, 0.80, 0.90。
  - beta = gamma = `(1 - alpha) / 2`。
  - candidate 的 `robust_score` 取所有 alpha 設定下的最小 score。
- 更新 candidate 接受條件。
  - 必須讓 robust score 比目前解更好。
  - 若 SS setup WNS/TNS 明顯變差則拒絕。
  - 若 FF hold WNS/TNS 嚴重變差則拒絕。
  - 若增加 area 但 timing/robust score 幾乎沒有意義改善則拒絕。
- 更新候選排序 tie-break。
  - 優先比較 SS setup WNS。
  - 再比較 SS setup TNS。
  - 再比較 robust score。
  - 接著考慮 hold degradation 與 area。
- 保留 540 秒 optimizer 內部時間限制。
  - 目標是在 10 分鐘限制前留下約 30 秒給最後 metrics 與輸出檔案。
- 已重新 `make` 編譯成功。
- 使用 testcase2 做 90 秒上限 sanity run。
  - 測試被外層 alarm 中止，未等待完整輸出。
  - 前 4 輪皆能找到並接受 robust-score 改善的候選。
  - log 已改為輸出 initial/final robust score 與 setup/hold/area score。

## 2026-06-17 Time-Budget Phase Scheduler 與 Group Edge Insertion

- 加入 time-budget phase scheduler。
  - `0s ~ 180s`: `SETUP_WNS`，優先修 setup WNS。
  - `180s ~ 420s`: `TNS_CLEANUP`，加入 top 1000 SS violations 的 TNS/NVP cleanup focus。
  - `420s ~ 510s`: `GROUP_INSERT`，加入 shared ancestor edge insertion。
  - `510s ~ 540s`: `HOLD_AREA`，偏向 hold/area cleanup。
- 新增 phase log。
  - 每輪 optimizer log 會印出目前 phase。
- 新增 shared ancestor edge insertion candidate。
  - 分析 top 1000 SS setup violated paths。
  - 將 capture FF 往上回溯 ancestor edge。
  - 依 edge 被 critical captures 共用的次數、累積 negative slack、worst slack 排序。
  - 對 top 160 shared edges 產生 internal insertion candidate。
  - 目標是一次 delay 一群 setup-critical capture FF，降低 leaf-only insertion 的 area 成本。
- 新增 area cleanup resize candidate。
  - 在 `HOLD_AREA` phase 嘗試把合法 buffer resize 成較小 area cell。
  - 仍需通過 robust score、setup/hold degradation guard。
- 依 phase 調整 candidate 來源。
  - `SETUP_WNS` 不再全開 TNS cleanup，降低早期候選數與每輪成本。
  - `TNS_CLEANUP` 才打開 top 1000 path 聚合。
  - `GROUP_INSERT` 才打開 shared ancestor edge insertion。
  - `HOLD_AREA` 強化 hold priority 並加入 area cleanup。
- 已重新 `make` 編譯成功。
- 使用 testcase2 做 90 秒上限 sanity run。
  - 測試被外層 alarm 中止，未等待完整輸出。
  - 前 90 秒停留在 `SETUP_WNS` phase，流程正常。
  - 候選數約 12k 到 13k，比全開 cleanup 時的 45k 到 46k 少。
  - 前 5 輪皆接受 robust-score 改善 candidate。
  - 觀察到 SS WNS 從 -0.578 改善到約 -0.4956，SS NVP 從 10898 附近改善到約 10890。

## 2026-06-17 回退 optimizer phase scheduler

- 依使用者要求，將 `optimizer.cpp` 退回上一版 optimizer。
- 移除剛新增的 time-budget phase scheduler。
  - 移除 `PhaseMode`。
  - 移除 `phaseForElapsed()` 與 `phaseName()`。
  - 主迴圈不再依時間切換 `SETUP_WNS`、`TNS_CLEANUP`、`GROUP_INSERT`、`HOLD_AREA` phase。
- 移除 shared ancestor edge insertion 實驗功能。
  - 移除 `EdgeStat`。
  - 移除 group edge priority 與 top shared edge candidate 產生邏輯。
- 移除 `HOLD_AREA` phase 專用的 area cleanup resize candidate。
- 保留上一版既有功能。
  - robust score 評分。
  - setup 優先與 hold degradation guard。
  - top 10 SS WNS repair 3-buffer chain candidate。
  - top 1000 SS TNS/NVP cleanup candidate。
  - 每輪最多 50000 candidates、8192 exact evaluations。
  - 540 秒 optimizer 內部時間限制。
- 已重新 `make` 編譯成功。
