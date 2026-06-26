# Optimization log

Priority order: setup timing first, then hold timing, then area.

Architecture: shared CandidateMove/ModeResult types; TimingEngine-owned normalized scoring, affected-path evaluation, and transactional rollback.

## Baseline
- Adaptive schedule selected from initial node/path/violation counts.
- Baseline SS TNS/WNS/NVP: -2277.689600/-0.593800/32270
- Baseline FF TNS/WNS/NVP: -6.990400/-0.121700/289
- Baseline area: 10539.493920
- Initial nodes/setup paths: 57427/120693
- Score terms initial score terms: SS_TNS_term=0.000000 SS_WNS_term=0.000000 FF_TNS_term=0.000000 FF_WNS_term=0.000000 AREA_term=0.000000 setup_sum=0.0000 hold_sum=0.0000 weakest_term=0.0000 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.0000,0.0000,0.0000,0.0000,0.0000 average_robust=0.0000 min_robust=0.0000 robust_score=0.0000 timing_score=0.0000 weakest=SS_TNS_term
- Phase setup_WNS_breakthrough directional_seed: 0.000000-18.000000 sec, exact limit 2048
- Phase setup_worst_path_surgery: 18.000000-83.000000 sec, exact limit 1200
- Phase setup_TNS_cleanup: 83.000000-83.100000 sec, exact limit 128
- Phase strong_hold_repair: 83.100000-89.000000 sec, exact limit 1536
- Phase balanced_area_cleanup: 88.000000-89.000000 sec, exact limit 512

## Phase: setup_WNS_breakthrough directional_seed
- Time window: 0.000000-18.000000 sec
- Score terms before setup_WNS_breakthrough directional_seed: SS_TNS_term=0.000000 SS_WNS_term=0.000000 FF_TNS_term=0.000000 FF_WNS_term=0.000000 AREA_term=0.000000 setup_sum=0.0000 hold_sum=0.0000 weakest_term=0.0000 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.0000,0.0000,0.0000,0.0000,0.0000 average_robust=0.0000 min_robust=0.0000 robust_score=0.0000 timing_score=0.0000 weakest=SS_TNS_term
- Iteration 0: generated 4000 candidates; chains none; resize speed/delay/area 0/4000/4000.
- Accepted move resize node=BUF_6 REALBUF_X10->REALBUF_X2; direction capture-side; old SS_TNS_term=0.000000 SS_WNS_term=0.000000 FF_TNS_term=0.000000 FF_WNS_term=0.000000 AREA_term=0.000000 setup_sum=0.0000 hold_sum=0.0000 weakest_term=0.0000 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.0000,0.0000,0.0000,0.0000,0.0000 average_robust=0.0000 min_robust=0.0000 robust_score=0.0000 timing_score=0.0000 weakest=SS_TNS_term; new SS_TNS_term=0.567865 SS_WNS_term=0.057258 FF_TNS_term=0.431535 FF_WNS_term=0.092030 AREA_term=0.087055 setup_sum=0.6251 hold_sum=0.5236 weakest_term=0.0573 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.4652,0.4972,0.5292,0.5612,0.5931 average_robust=0.5292 min_robust=0.4652 robust_score=0.5292 timing_score=2.8075 weakest=SS_WNS_term; delta dSS_TNS=0.567865 dSS_WNS=0.057258 dFF_TNS=0.431535 dFF_WNS=0.092030 dAREA=0.087055 dROBUST=0.529179 dTIMING=2.807516; reason: weakest normalized term improved.
  - Progress mode: improving the weakest term.
- Iteration 0: accepted downsize for useful delay sequence; evaluated 2048; sequence length 1352; SS TNS/WNS/NVP -984.270400/-0.559800/19144; FF TNS/WNS/NVP -3.973800/-0.110500/141; area 9621.975120.
  - Updated best_tree.
- Iteration 1: generated 4000 candidates; chains none; resize speed/delay/area 0/4000/4000.
- Accepted move resize node=BUF_109 REALBUF_X10->REALBUF_X2; direction capture-side; old SS_TNS_term=0.567865 SS_WNS_term=0.057258 FF_TNS_term=0.431535 FF_WNS_term=0.092030 AREA_term=0.087055 setup_sum=0.6251 hold_sum=0.5236 weakest_term=0.0573 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.4652,0.4972,0.5292,0.5612,0.5931 average_robust=0.5292 min_robust=0.4652 robust_score=0.5292 timing_score=2.8075 weakest=SS_WNS_term; new SS_TNS_term=0.626330 SS_WNS_term=0.057258 FF_TNS_term=0.430476 FF_WNS_term=0.092030 AREA_term=0.143364 setup_sum=0.6836 hold_sum=0.5225 weakest_term=0.0573 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.5083,0.5433,0.5784,0.6135,0.6485 average_robust=0.5784 min_robust=0.5083 robust_score=0.5784 timing_score=3.0095 weakest=SS_WNS_term; delta dSS_TNS=0.058465 dSS_WNS=0.000000 dFF_TNS=-0.001059 dFF_WNS=0.000000 dAREA=0.056309 dROBUST=0.049213 dTIMING=0.201963; reason: timing score improved with protected terms.
  - Progress mode: improving timing-first normalized score.
- Iteration 1: accepted downsize for useful delay sequence; evaluated 1450; sequence length 843; SS TNS/WNS/NVP -851.104500/-0.559800/16849; FF TNS/WNS/NVP -3.981200/-0.110500/141; area 9028.505320.
  - Updated best_tree.
- Score terms after setup_WNS_breakthrough directional_seed: SS_TNS_term=0.626330 SS_WNS_term=0.057258 FF_TNS_term=0.430476 FF_WNS_term=0.092030 AREA_term=0.143364 setup_sum=0.6836 hold_sum=0.5225 weakest_term=0.0573 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.5083,0.5433,0.5784,0.6135,0.6485 average_robust=0.5784 min_robust=0.5083 robust_score=0.5784 timing_score=3.0095 weakest=SS_WNS_term

## Phase: setup_worst_path_surgery
- Time window: 18.000000-83.000000 sec
- Score terms before setup_worst_path_surgery: SS_TNS_term=0.626330 SS_WNS_term=0.057258 FF_TNS_term=0.430476 FF_WNS_term=0.092030 AREA_term=0.143364 setup_sum=0.6836 hold_sum=0.5225 weakest_term=0.0573 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.5083,0.5433,0.5784,0.6135,0.6485 average_robust=0.5784 min_robust=0.5083 robust_score=0.5784 timing_score=3.0095 weakest=SS_WNS_term
- Iteration 0: generated 801 candidates; chains L4=18, L5=18, L6=31, L7=44, L8=75; resize speed/delay/area 595/20/20.
- Iteration 0: evaluated 801, no acceptable candidate.
- Iteration 1: generated 801 candidates; chains L4=18, L5=18, L6=31, L7=44, L8=75; resize speed/delay/area 595/20/20.
- Iteration 1: evaluated 801, no acceptable candidate.
- Score terms after setup_worst_path_surgery: SS_TNS_term=0.626330 SS_WNS_term=0.057258 FF_TNS_term=0.430476 FF_WNS_term=0.092030 AREA_term=0.143364 setup_sum=0.6836 hold_sum=0.5225 weakest_term=0.0573 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.5083,0.5433,0.5784,0.6135,0.6485 average_robust=0.5784 min_robust=0.5083 robust_score=0.5784 timing_score=3.0095 weakest=SS_WNS_term

## Phase: setup_TNS_cleanup
- Time window: 83.000000-83.100000 sec
- Score terms before setup_TNS_cleanup: SS_TNS_term=0.626330 SS_WNS_term=0.057258 FF_TNS_term=0.430476 FF_WNS_term=0.092030 AREA_term=0.143364 setup_sum=0.6836 hold_sum=0.5225 weakest_term=0.0573 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.5083,0.5433,0.5784,0.6135,0.6485 average_robust=0.5784 min_robust=0.5083 robust_score=0.5784 timing_score=3.0095 weakest=SS_WNS_term
- Iteration 0: generated 4000 candidates; chains L7=177, L8=1702; resize speed/delay/area 879/1242/1223.
- Iteration 0: evaluated 128, no acceptable candidate.
- Score terms after setup_TNS_cleanup: SS_TNS_term=0.626330 SS_WNS_term=0.057258 FF_TNS_term=0.430476 FF_WNS_term=0.092030 AREA_term=0.143364 setup_sum=0.6836 hold_sum=0.5225 weakest_term=0.0573 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.5083,0.5433,0.5784,0.6135,0.6485 average_robust=0.5784 min_robust=0.5083 robust_score=0.5784 timing_score=3.0095 weakest=SS_WNS_term

## Phase: strong_hold_repair
- Time window: 83.100000-89.000000 sec
- Score terms before strong_hold_repair: SS_TNS_term=0.626330 SS_WNS_term=0.057258 FF_TNS_term=0.430476 FF_WNS_term=0.092030 AREA_term=0.143364 setup_sum=0.6836 hold_sum=0.5225 weakest_term=0.0573 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.5083,0.5433,0.5784,0.6135,0.6485 average_robust=0.5784 min_robust=0.5083 robust_score=0.5784 timing_score=3.0095 weakest=SS_WNS_term
- Iteration 0: generated 4000 candidates; chains L1=3, L2=39, L3=32, L4=35, L5=42, L6=105, L7=217, L8=1477; resize speed/delay/area 1951/99/99.
- Accepted move insert-internal edge=BUF_25->FF_25; direction launch-side; old SS_TNS_term=0.626330 SS_WNS_term=0.057258 FF_TNS_term=0.430476 FF_WNS_term=0.092030 AREA_term=0.143364 setup_sum=0.6836 hold_sum=0.5225 weakest_term=0.0573 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.5083,0.5433,0.5784,0.6135,0.6485 average_robust=0.5784 min_robust=0.5083 robust_score=0.5784 timing_score=3.0095 weakest=SS_WNS_term; new SS_TNS_term=0.642942 SS_WNS_term=0.057258 FF_TNS_term=0.554303 FF_WNS_term=0.236647 AREA_term=0.141597 setup_sum=0.7002 hold_sum=0.7910 weakest_term=0.0573 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.5832,0.6066,0.6300,0.6534,0.6768 average_robust=0.6300 min_robust=0.5832 robust_score=0.6300 timing_score=3.5334 weakest=SS_WNS_term; delta dSS_TNS=0.016612 dSS_WNS=0.000000 dFF_TNS=0.123827 dFF_WNS=0.144618 dAREA=-0.001767 dROBUST=0.051630 dTIMING=0.523928; reason: timing score improved with protected terms.
  - Progress mode: improving timing-first normalized score.
- Iteration 0: accepted hold final repair sequence; evaluated 1536; sequence length 17; SS TNS/WNS/NVP -813.267600/-0.559800/16387; FF TNS/WNS/NVP -3.115600/-0.092900/117; area 9047.132664.
  - Updated best_tree.
- Iteration 1: generated 4000 candidates; chains L1=8, L2=38, L3=56, L4=45, L5=49, L6=92, L7=200, L8=1464; resize speed/delay/area 1952/96/96.
- Accepted move insert-internal edge=BUF_37->FF_37; direction launch-side; old SS_TNS_term=0.642942 SS_WNS_term=0.057258 FF_TNS_term=0.554303 FF_WNS_term=0.236647 AREA_term=0.141597 setup_sum=0.7002 hold_sum=0.7910 weakest_term=0.0573 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.5832,0.6066,0.6300,0.6534,0.6768 average_robust=0.6300 min_robust=0.5832 robust_score=0.6300 timing_score=3.5334 weakest=SS_WNS_term; new SS_TNS_term=0.648342 SS_WNS_term=0.057258 FF_TNS_term=0.622382 FF_WNS_term=0.241578 AREA_term=0.139966 setup_sum=0.7056 hold_sum=0.8640 weakest_term=0.0573 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.6038,0.6241,0.6445,0.6649,0.6852 average_robust=0.6445 min_robust=0.6038 robust_score=0.6445 timing_score=3.6608 weakest=SS_WNS_term; delta dSS_TNS=0.005400 dSS_WNS=0.000000 dFF_TNS=0.068079 dFF_WNS=0.004930 dAREA=-0.001631 dROBUST=0.014487 dTIMING=0.127364; reason: timing score improved with protected terms.
  - Progress mode: improving timing-first normalized score.
- Iteration 1: accepted hold final repair sequence; evaluated 1536; sequence length 17; SS TNS/WNS/NVP -800.968000/-0.559800/16220; FF TNS/WNS/NVP -2.639700/-0.092300/101; area 9064.320344.
  - Updated best_tree.
- Iteration 2: generated 4000 candidates; chains L1=2, L2=35, L3=101, L4=73, L5=76, L6=133, L7=240, L8=1477; resize speed/delay/area 1770/93/105.
- Accepted move insert-internal edge=BUF_157->FF_147; direction launch-side; old SS_TNS_term=0.648342 SS_WNS_term=0.057258 FF_TNS_term=0.622382 FF_WNS_term=0.241578 AREA_term=0.139966 setup_sum=0.7056 hold_sum=0.8640 weakest_term=0.0573 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.6038,0.6241,0.6445,0.6649,0.6852 average_robust=0.6445 min_robust=0.6038 robust_score=0.6445 timing_score=3.6608 weakest=SS_WNS_term; new SS_TNS_term=0.650555 SS_WNS_term=0.057258 FF_TNS_term=0.657187 FF_WNS_term=0.331964 AREA_term=0.138549 setup_sum=0.7078 hold_sum=0.9892 weakest_term=0.0573 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.6358,0.6502,0.6646,0.6790,0.6934 average_robust=0.6646 min_robust=0.6358 robust_score=0.6646 timing_score=3.8997 weakest=SS_WNS_term; delta dSS_TNS=0.002213 dSS_WNS=0.000000 dFF_TNS=0.034805 dFF_WNS=0.090386 dAREA=-0.001418 dROBUST=0.020115 dTIMING=0.238911; reason: timing score improved with protected terms.
  - Progress mode: improving timing-first normalized score.
- Iteration 2: accepted hold final repair sequence; evaluated 1536; sequence length 17; SS TNS/WNS/NVP -795.926400/-0.559800/16173; FF TNS/WNS/NVP -2.396400/-0.081300/91; area 9079.260240.
  - Updated best_tree.
- Iteration 3: generated 4000 candidates; chains L1=2, L2=28, L3=91, L4=111, L5=98, L6=131, L7=244, L8=1601; resize speed/delay/area 1605/89/101.
- Accepted move resize node=BUF_16797 REALBUF_X2->REALBUF_X16; direction launch-side; old SS_TNS_term=0.650555 SS_WNS_term=0.057258 FF_TNS_term=0.657187 FF_WNS_term=0.331964 AREA_term=0.138549 setup_sum=0.7078 hold_sum=0.9892 weakest_term=0.0573 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.6358,0.6502,0.6646,0.6790,0.6934 average_robust=0.6646 min_robust=0.6358 robust_score=0.6646 timing_score=3.8997 weakest=SS_WNS_term; new SS_TNS_term=0.651228 SS_WNS_term=0.057258 FF_TNS_term=0.701162 FF_WNS_term=0.352506 AREA_term=0.136963 setup_sum=0.7085 hold_sum=1.0537 weakest_term=0.0573 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.6519,0.6632,0.6745,0.6859,0.6972 average_robust=0.6745 min_robust=0.6519 robust_score=0.6745 timing_score=4.0080 weakest=SS_WNS_term; delta dSS_TNS=0.000672 dSS_WNS=0.000000 dFF_TNS=0.043975 dFF_WNS=0.020542 dAREA=-0.001586 dROBUST=0.009910 dTIMING=0.108271; reason: timing score improved with protected terms.
  - Progress mode: improving timing-first normalized score.
- Iteration 3: accepted hold final repair sequence; evaluated 1536; sequence length 16; SS TNS/WNS/NVP -794.394800/-0.559800/16189; FF TNS/WNS/NVP -2.089000/-0.078800/82; area 9095.973016.
  - Updated best_tree.
- Iteration 4: generated 4000 candidates; chains L1=4, L2=29, L3=79, L4=114, L5=121, L6=162, L7=266, L8=1705; resize speed/delay/area 1443/77/89.
- Accepted move resize node=BUF_9884 REALBUF_X2->REALBUF_X16; direction launch-side; old SS_TNS_term=0.651228 SS_WNS_term=0.057258 FF_TNS_term=0.701162 FF_WNS_term=0.352506 AREA_term=0.136963 setup_sum=0.7085 hold_sum=1.0537 weakest_term=0.0573 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.6519,0.6632,0.6745,0.6859,0.6972 average_robust=0.6745 min_robust=0.6519 robust_score=0.6745 timing_score=4.0080 weakest=SS_WNS_term; new SS_TNS_term=0.652064 SS_WNS_term=0.057258 FF_TNS_term=0.736996 FF_WNS_term=0.352506 AREA_term=0.135219 setup_sum=0.7093 hold_sum=1.0895 weakest_term=0.0573 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.6608,0.6705,0.6802,0.6899,0.6996 average_robust=0.6802 min_robust=0.6608 robust_score=0.6802 timing_score=4.0633 weakest=SS_WNS_term; delta dSS_TNS=0.000836 dSS_WNS=0.000000 dFF_TNS=0.035835 dFF_WNS=0.000000 dAREA=-0.001744 dROBUST=0.005699 dTIMING=0.055389; reason: timing score improved with protected terms.
  - Progress mode: improving timing-first normalized score.
- Iteration 4: accepted hold final repair sequence; evaluated 1536; sequence length 16; SS TNS/WNS/NVP -792.490200/-0.559800/16226; FF TNS/WNS/NVP -1.838500/-0.078800/73; area 9114.356856.
  - Updated best_tree.
- Iteration 5: generated 4000 candidates; chains L1=4, L2=35, L3=84, L4=106, L5=131, L6=160, L7=281, L8=1881; resize speed/delay/area 1258/60/71.
- Accepted move resize node=BUF_6785 REALBUF_X2->REALBUF_X16; direction launch-side; old SS_TNS_term=0.652064 SS_WNS_term=0.057258 FF_TNS_term=0.736996 FF_WNS_term=0.352506 AREA_term=0.135219 setup_sum=0.7093 hold_sum=1.0895 weakest_term=0.0573 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.6608,0.6705,0.6802,0.6899,0.6996 average_robust=0.6802 min_robust=0.6608 robust_score=0.6802 timing_score=4.0633 weakest=SS_WNS_term; new SS_TNS_term=0.651784 SS_WNS_term=0.057258 FF_TNS_term=0.762889 FF_WNS_term=0.352506 AREA_term=0.133750 setup_sum=0.7090 hold_sum=1.1154 weakest_term=0.0573 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.6668,0.6753,0.6837,0.6921,0.7006 average_robust=0.6837 min_robust=0.6668 robust_score=0.6837 timing_score=4.1006 weakest=SS_WNS_term; delta dSS_TNS=-0.000280 dSS_WNS=0.000000 dFF_TNS=0.025893 dFF_WNS=0.000000 dAREA=-0.001469 dROBUST=0.003468 dTIMING=0.037265; reason: timing score improved with protected terms.
  - Progress mode: improving timing-first normalized score.
- Iteration 5: accepted hold final repair sequence; evaluated 1536; sequence length 17; SS TNS/WNS/NVP -793.127700/-0.559800/16265; FF TNS/WNS/NVP -1.657500/-0.078800/59; area 9129.837872.
  - Updated best_tree.
- Iteration 6: generated 4000 candidates; chains L1=3, L2=30, L3=69, L4=89, L5=113, L6=142, L7=245, L8=2210; resize speed/delay/area 1045/54/62.
- Accepted move resize node=BUF_7224 REALBUF_X2->REALBUF_X16; direction launch-side; old SS_TNS_term=0.651784 SS_WNS_term=0.057258 FF_TNS_term=0.762889 FF_WNS_term=0.352506 AREA_term=0.133750 setup_sum=0.7090 hold_sum=1.1154 weakest_term=0.0573 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.6668,0.6753,0.6837,0.6921,0.7006 average_robust=0.6837 min_robust=0.6668 robust_score=0.6837 timing_score=4.1006 weakest=SS_WNS_term; new SS_TNS_term=0.651735 SS_WNS_term=0.057258 FF_TNS_term=0.816148 FF_WNS_term=0.352506 AREA_term=0.132473 setup_sum=0.7090 hold_sum=1.1687 weakest_term=0.0573 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.6798,0.6856,0.6915,0.6973,0.7032 average_robust=0.6915 min_robust=0.6798 robust_score=0.6915 timing_score=4.1797 weakest=SS_WNS_term; delta dSS_TNS=-0.000049 dSS_WNS=0.000000 dFF_TNS=0.053259 dFF_WNS=0.000000 dAREA=-0.001277 dROBUST=0.007763 dTIMING=0.079102; reason: timing score improved with protected terms.
  - Progress mode: improving timing-first normalized score.
- Iteration 6: accepted hold final repair sequence; evaluated 1536; sequence length 17; SS TNS/WNS/NVP -793.240100/-0.559800/16323; FF TNS/WNS/NVP -1.285200/-0.078800/46; area 9143.291824.
  - Updated best_tree.
- Iteration 7: generated 3614 candidates; chains L2=17, L3=33, L4=59, L5=90, L6=118, L7=219, L8=2210; resize speed/delay/area 817/51/59.
- Accepted move resize node=BUF_11447 REALBUF_X2->REALBUF_X16; direction launch-side; old SS_TNS_term=0.651735 SS_WNS_term=0.057258 FF_TNS_term=0.816148 FF_WNS_term=0.352506 AREA_term=0.132473 setup_sum=0.7090 hold_sum=1.1687 weakest_term=0.0573 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.6798,0.6856,0.6915,0.6973,0.7032 average_robust=0.6915 min_robust=0.6798 robust_score=0.6915 timing_score=4.1797 weakest=SS_WNS_term; new SS_TNS_term=0.650595 SS_WNS_term=0.057258 FF_TNS_term=0.847605 FF_WNS_term=0.368940 AREA_term=0.131482 setup_sum=0.7079 hold_sum=1.2165 weakest_term=0.0573 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.6909,0.6943,0.6977,0.7011,0.7045 average_robust=0.6977 min_robust=0.6909 robust_score=0.6977 timing_score=4.2558 weakest=SS_WNS_term; delta dSS_TNS=-0.001140 dSS_WNS=0.000000 dFF_TNS=0.031457 dFF_WNS=0.016434 dAREA=-0.000991 dROBUST=0.006237 dTIMING=0.076139; reason: timing score improved with protected terms.
  - Progress mode: improving timing-first normalized score.
- Iteration 7: accepted hold final repair sequence; evaluated 1536; sequence length 16; SS TNS/WNS/NVP -795.836100/-0.559800/16374; FF TNS/WNS/NVP -1.065300/-0.076800/33; area 9153.739000.
  - Updated best_tree.
- Iteration 8: generated 2657 candidates; chains L1=1, L2=6, L3=13, L4=26, L5=52, L6=73, L7=160, L8=1719; resize speed/delay/area 568/39/45.
- Accepted move resize node=BUF_12611 REALBUF_X2->REALBUF_X16; direction launch-side; old SS_TNS_term=0.650595 SS_WNS_term=0.057258 FF_TNS_term=0.847605 FF_WNS_term=0.368940 AREA_term=0.131482 setup_sum=0.7079 hold_sum=1.2165 weakest_term=0.0573 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.6909,0.6943,0.6977,0.7011,0.7045 average_robust=0.6977 min_robust=0.6909 robust_score=0.6977 timing_score=4.2558 weakest=SS_WNS_term; new SS_TNS_term=0.651108 SS_WNS_term=0.070731 FF_TNS_term=0.882982 FF_WNS_term=0.387839 AREA_term=0.130503 setup_sum=0.7218 hold_sum=1.2708 weakest_term=0.0707 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.7113,0.7134,0.7155,0.7176,0.7197 average_robust=0.7155 min_robust=0.7113 robust_score=0.7155 timing_score=4.4016 weakest=SS_WNS_term; delta dSS_TNS=0.000513 dSS_WNS=0.013473 dFF_TNS=0.035377 dFF_WNS=0.018899 dAREA=-0.000979 dROBUST=0.017784 dTIMING=0.145802; reason: weakest normalized term improved.
  - Progress mode: improving the weakest term.
- Iteration 8: accepted hold final repair sequence; evaluated 1536; sequence length 15; SS TNS/WNS/NVP -794.668300/-0.551800/16396; FF TNS/WNS/NVP -0.818000/-0.074500/25; area 9164.060152.
  - Updated best_tree.
- Iteration 9: generated 2132 candidates; chains L2=10, L3=27, L4=28, L5=44, L6=69, L7=120, L8=1366; resize speed/delay/area 438/30/33.
- Accepted move resize node=BUF_4166 REALBUF_X4->REALBUF_X16; direction launch-side; old SS_TNS_term=0.651108 SS_WNS_term=0.070731 FF_TNS_term=0.882982 FF_WNS_term=0.387839 AREA_term=0.130503 setup_sum=0.7218 hold_sum=1.2708 weakest_term=0.0707 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.7113,0.7134,0.7155,0.7176,0.7197 average_robust=0.7155 min_robust=0.7113 robust_score=0.7155 timing_score=4.4016 weakest=SS_WNS_term; new SS_TNS_term=0.653352 SS_WNS_term=0.083530 FF_TNS_term=0.953593 FF_WNS_term=0.569433 AREA_term=0.129517 setup_sum=0.7369 hold_sum=1.5230 weakest_term=0.0835 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.7816,0.7726,0.7637,0.7548,0.7458 average_robust=0.7637 min_robust=0.7458 robust_score=0.7637 timing_score=4.9282 weakest=SS_WNS_term; delta dSS_TNS=0.002244 dSS_WNS=0.012799 dFF_TNS=0.070611 dFF_WNS=0.181594 dAREA=-0.000986 dROBUST=0.048213 dTIMING=0.526540; reason: weakest normalized term improved.
  - Progress mode: improving the weakest term.
- Iteration 9: accepted hold final repair sequence; evaluated 1536; sequence length 15; SS TNS/WNS/NVP -789.556600/-0.544200/16408; FF TNS/WNS/NVP -0.324400/-0.052400/13; area 9174.451080.
  - Updated best_tree.
- Iteration 10: generated 1169 candidates; chains L2=6, L3=13, L4=19, L5=26, L6=38, L7=54, L8=772; resize speed/delay/area 219/22/22.
- Accepted move resize node=BUF_5078 REALBUF_X6->REALBUF_X16; direction launch-side; old SS_TNS_term=0.653352 SS_WNS_term=0.083530 FF_TNS_term=0.953593 FF_WNS_term=0.569433 AREA_term=0.129517 setup_sum=0.7369 hold_sum=1.5230 weakest_term=0.0835 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.7816,0.7726,0.7637,0.7548,0.7458 average_robust=0.7637 min_robust=0.7458 robust_score=0.7637 timing_score=4.9282 weakest=SS_WNS_term; new SS_TNS_term=0.657834 SS_WNS_term=0.083530 FF_TNS_term=0.992146 FF_WNS_term=0.741988 AREA_term=0.128806 setup_sum=0.7414 hold_sum=1.7341 weakest_term=0.0835 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.8364,0.8174,0.7984,0.7794,0.7604 average_robust=0.7984 min_robust=0.7604 robust_score=0.7984 timing_score=5.3442 weakest=SS_WNS_term; delta dSS_TNS=0.004482 dSS_WNS=0.000000 dFF_TNS=0.038553 dFF_WNS=0.172555 dAREA=-0.000710 dROBUST=0.034697 dTIMING=0.416030; reason: timing score improved with protected terms.
  - Progress mode: improving timing-first normalized score.
- Iteration 10: accepted hold final repair sequence; evaluated 1122; sequence length 10; SS TNS/WNS/NVP -779.348400/-0.544200/16421; FF TNS/WNS/NVP -0.054900/-0.031400/4; area 9181.939184.
  - Updated best_tree.
- Iteration 11: generated 394 candidates; chains L2=1, L3=4, L4=2, L5=6, L6=8, L7=14, L8=267; resize speed/delay/area 80/12/12.
- Accepted move resize node=BUF_7104 REALBUF_X6->REALBUF_X16; direction launch-side; old SS_TNS_term=0.657834 SS_WNS_term=0.083530 FF_TNS_term=0.992146 FF_WNS_term=0.741988 AREA_term=0.128806 setup_sum=0.7414 hold_sum=1.7341 weakest_term=0.0835 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.8364,0.8174,0.7984,0.7794,0.7604 average_robust=0.7984 min_robust=0.7604 robust_score=0.7984 timing_score=5.3442 weakest=SS_WNS_term; new SS_TNS_term=0.660619 SS_WNS_term=0.242337 FF_TNS_term=0.999657 FF_WNS_term=0.980279 AREA_term=0.128521 setup_sum=0.9030 hold_sum=1.9799 weakest_term=0.2423 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.9786,0.9635,0.9483,0.9332,0.9181 average_robust=0.9483 min_robust=0.9181 robust_score=0.9483 timing_score=6.4755 weakest=SS_WNS_term; delta dSS_TNS=0.002786 dSS_WNS=0.158808 dFF_TNS=0.007510 dFF_WNS=0.238291 dAREA=-0.000285 dROBUST=0.149943 dTIMING=1.131292; reason: weakest normalized term improved.
  - Progress mode: improving the weakest term.
- Iteration 11: accepted hold final repair sequence; evaluated 381; sequence length 4; SS TNS/WNS/NVP -773.003600/-0.449900/16412; FF TNS/WNS/NVP -0.002400/-0.002400/1; area 9184.945960.
  - Updated best_tree.
- Iteration 12: generated 127 candidates; chains L7=1, L8=97; resize speed/delay/area 27/2/2.
- Accepted move insert-internal edge=BUF_9->FF_11; direction launch-side; old SS_TNS_term=0.660619 SS_WNS_term=0.242337 FF_TNS_term=0.999657 FF_WNS_term=0.980279 AREA_term=0.128521 setup_sum=0.9030 hold_sum=1.9799 weakest_term=0.2423 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.9786,0.9635,0.9483,0.9332,0.9181 average_robust=0.9483 min_robust=0.9181 robust_score=0.9483 timing_score=6.4755 weakest=SS_WNS_term; new SS_TNS_term=0.662239 SS_WNS_term=0.242337 FF_TNS_term=1.000000 FF_WNS_term=1.000000 AREA_term=0.128425 setup_sum=0.9046 hold_sum=2.0000 weakest_term=0.2423 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.9844,0.9684,0.9525,0.9365,0.9205 average_robust=0.9525 min_robust=0.9205 robust_score=0.9525 timing_score=6.5203 weakest=SS_WNS_term; delta dSS_TNS=0.001620 dSS_WNS=0.000000 dFF_TNS=0.000343 dFF_WNS=0.019721 dAREA=-0.000096 dROBUST=0.004129 dTIMING=0.044767; reason: timing score improved with protected terms.
  - Progress mode: improving timing-first normalized score.
- Iteration 12: accepted hold final repair sequence; evaluated 117; sequence length 1; SS TNS/WNS/NVP -769.314500/-0.449900/16407; FF TNS/WNS/NVP 0.000000/0.000000/0; area 9185.959848.
  - Updated best_tree.
- Iteration 13: no legal candidates.
- Score terms after strong_hold_repair: SS_TNS_term=0.662239 SS_WNS_term=0.242337 FF_TNS_term=1.000000 FF_WNS_term=1.000000 AREA_term=0.128425 setup_sum=0.9046 hold_sum=2.0000 weakest_term=0.2423 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.9844,0.9684,0.9525,0.9365,0.9205 average_robust=0.9525 min_robust=0.9205 robust_score=0.9525 timing_score=6.5203 weakest=SS_WNS_term

## Phase: balanced_area_cleanup
- Time window: 88.000000-89.000000 sec
- Score terms before balanced_area_cleanup: SS_TNS_term=0.662239 SS_WNS_term=0.242337 FF_TNS_term=1.000000 FF_WNS_term=1.000000 AREA_term=0.128425 setup_sum=0.9046 hold_sum=2.0000 weakest_term=0.2423 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.9844,0.9684,0.9525,0.9365,0.9205 average_robust=0.9525 min_robust=0.9205 robust_score=0.9525 timing_score=6.5203 weakest=SS_WNS_term
- Score terms after balanced_area_cleanup: SS_TNS_term=0.662239 SS_WNS_term=0.242337 FF_TNS_term=1.000000 FF_WNS_term=1.000000 AREA_term=0.128425 setup_sum=0.9046 hold_sum=2.0000 weakest_term=0.2423 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.9844,0.9684,0.9525,0.9365,0.9205 average_robust=0.9525 min_robust=0.9205 robust_score=0.9525 timing_score=6.5203 weakest=SS_WNS_term

```
========== Mode Comparison: testcase0 ==========
mode                    SS_TNS    SS_WNS    FF_TNS    FF_WNS      AREA    timing_score    robust_score     runtime
baseline                0.0000    0.0000    0.0000    0.0000    0.0000          0.0000          0.0000        0.2s
setup_WNS_breakthrough_seed    0.6263    0.0573    0.4305    0.0920    0.1434          3.0095          0.5784       18.2s
setup_worst_path_surgery    0.6263    0.0573    0.4305    0.0920    0.1434          3.0095          0.5784       28.9s
setup_TNS_cleanup       0.6263    0.0573    0.4305    0.0920    0.1434          3.0095          0.5784       32.7s
strong_hold_repair      0.6622    0.2423    1.0000    1.0000    0.1284          6.5203          0.9525       88.2s
safe_area_cleanup       0.6622    0.2423    1.0000    1.0000    0.1284          6.5203          0.9525       88.2s
selected_best           0.6622    0.2423    1.0000    1.0000    0.1284          6.5203          0.9525       88.2s
=====================================
```
- Score terms final selected solution (strong_hold_repair): SS_TNS_term=0.662239 SS_WNS_term=0.242337 FF_TNS_term=1.000000 FF_WNS_term=1.000000 AREA_term=0.128425 setup_sum=0.9046 hold_sum=2.0000 weakest_term=0.2423 bottleneck=setup repair robust[0.50,0.60,0.70,0.80,0.90]=0.9844,0.9684,0.9525,0.9365,0.9205 average_robust=0.9525 min_robust=0.9205 robust_score=0.9525 timing_score=6.5203 weakest=SS_WNS_term

## Final
- Final best robust score: 0.952467
- Final SS TNS/WNS/NVP: -769.314500/-0.449900/16407
- Final FF TNS/WNS/NVP: 0.000000/0.000000/0
- Final area: 9185.959848
- Applied moves: 2373; inserted buffers: 473; resize moves: 2313
- Aggressive setup worsened hold temporarily: no; hold repair recovered: no.
