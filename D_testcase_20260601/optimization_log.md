# Optimization log

Priority order: setup timing first, then hold timing, then area.

## Baseline
- Existing optimizer status: it did not have fixed 0-2/2-6/6-8/8-9/9-9.5 minute phases; it used one mixed greedy loop.
- New schedule: 0-2 WNS repair, 2-6 TNS/NVP cleanup, 6-8 group/ancestor insertion, 8-9 hold final repair, 9-9.5 area recovery, then output best_tree.
- Baseline SS TNS/WNS/NVP: -1134.160100/-0.528600/18745
- Baseline FF TNS/WNS/NVP: -2.988000/-0.106500/121
- Baseline area: 6997.009120

## Phase: WNS repair
- Time window: 0.000000-70.000000 sec
- Iteration 0: accepted WNS repair move; evaluated 384; SS TNS/WNS/NVP -1132.660100/-0.491600/18745; FF TNS/WNS/NVP -2.859100/-0.106500/119; area 6997.389328.
  - Updated best_tree.
- Iteration 1: accepted WNS repair move; evaluated 384; SS TNS/WNS/NVP -1131.160100/-0.473700/18745; FF TNS/WNS/NVP -2.820400/-0.106500/118; area 6997.769536.
  - Updated best_tree.
- Iteration 2: accepted WNS repair move; evaluated 384; SS TNS/WNS/NVP -1131.010100/-0.473300/18745; FF TNS/WNS/NVP -2.772300/-0.106500/115; area 6998.149744.
  - Updated best_tree.
- Iteration 3: accepted WNS repair move; evaluated 384; SS TNS/WNS/NVP -1129.510100/-0.459300/18745; FF TNS/WNS/NVP -2.772300/-0.106500/115; area 6998.529952.
  - Updated best_tree.
- Iteration 4: accepted WNS repair move; evaluated 384; SS TNS/WNS/NVP -1129.285600/-0.452600/18744; FF TNS/WNS/NVP -2.728400/-0.106500/114; area 6998.910160.
  - Updated best_tree.
- Iteration 5: accepted WNS repair move; evaluated 384; SS TNS/WNS/NVP -1127.910500/-0.421600/18743; FF TNS/WNS/NVP -2.728400/-0.106500/114; area 6999.290368.
  - Updated best_tree.
- Iteration 6: accepted WNS repair move; evaluated 384; SS TNS/WNS/NVP -1127.610500/-0.420300/18743; FF TNS/WNS/NVP -2.627700/-0.106500/112; area 6999.670576.
  - Updated best_tree.
- Iteration 7: accepted WNS repair move; evaluated 384; SS TNS/WNS/NVP -1127.310500/-0.414500/18743; FF TNS/WNS/NVP -2.479500/-0.106500/110; area 7000.050784.
  - Updated best_tree.
- Iteration 8: accepted WNS repair move; evaluated 384; SS TNS/WNS/NVP -1127.127400/-0.410500/18742; FF TNS/WNS/NVP -2.471100/-0.106500/109; area 7000.430992.
  - Updated best_tree.
- Iteration 9: accepted WNS repair move; evaluated 384; SS TNS/WNS/NVP -1126.977400/-0.403500/18742; FF TNS/WNS/NVP -2.429700/-0.106500/107; area 7000.811200.
  - Updated best_tree.
- Iteration 10: accepted WNS repair move; evaluated 384; SS TNS/WNS/NVP -1125.477400/-0.396800/18742; FF TNS/WNS/NVP -2.429700/-0.106500/107; area 7001.191408.
  - Updated best_tree.
- Iteration 11: accepted WNS repair move; evaluated 384; SS TNS/WNS/NVP -1124.877400/-0.389600/18742; FF TNS/WNS/NVP -2.421900/-0.106500/106; area 7001.571616.
  - Updated best_tree.
- Iteration 12: accepted WNS repair move; evaluated 384; SS TNS/WNS/NVP -1123.553400/-0.388100/18739; FF TNS/WNS/NVP -2.403500/-0.106500/105; area 7001.951824.
  - Updated best_tree.
- Iteration 13: accepted WNS repair move; evaluated 83; SS TNS/WNS/NVP -1123.138000/-0.384500/18737; FF TNS/WNS/NVP -2.318400/-0.106500/102; area 7002.332032.
  - Updated best_tree.

## Phase: TNS/NVP cleanup
- Time window: 70.000000-420.000000 sec
- Iteration 0: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -1079.608100/-0.384500/18110; FF TNS/WNS/NVP -2.232600/-0.106500/98; area 7003.564504.
  - Updated best_tree.
- Iteration 1: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -1034.052900/-0.393100/17573; FF TNS/WNS/NVP -2.234200/-0.106500/96; area 7004.796976.
- Iteration 2: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -970.171400/-0.393100/17164; FF TNS/WNS/NVP -1.719500/-0.106500/74; area 7005.050448.
- Iteration 3: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -948.660500/-0.399700/16886; FF TNS/WNS/NVP -1.644800/-0.106500/70; area 7005.891320.
- Iteration 4: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -925.454700/-0.408900/16566; FF TNS/WNS/NVP -1.669900/-0.106500/70; area 7006.213856.
- Iteration 5: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -894.190800/-0.408900/16279; FF TNS/WNS/NVP -1.563500/-0.106500/65; area 7006.663128.
- Iteration 6: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -878.627700/-0.408900/16158; FF TNS/WNS/NVP -1.540200/-0.106500/63; area 7006.916600.
- Iteration 7: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -867.371100/-0.408900/16039; FF TNS/WNS/NVP -1.540200/-0.106500/63; area 7007.170072.
- Iteration 8: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -859.549400/-0.408900/15919; FF TNS/WNS/NVP -1.540200/-0.106500/63; area 7007.492608.
- Iteration 9: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -855.539400/-0.408900/15795; FF TNS/WNS/NVP -1.571500/-0.106500/65; area 7009.312480.
- Iteration 10: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -844.329900/-0.408900/15678; FF TNS/WNS/NVP -1.550500/-0.106500/64; area 7009.439216.
- Iteration 11: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -837.470100/-0.408900/15580; FF TNS/WNS/NVP -1.573100/-0.106500/64; area 7010.084288.
- Iteration 12: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -832.148200/-0.408900/15523; FF TNS/WNS/NVP -1.547100/-0.106500/63; area 7010.533560.
- Iteration 13: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -828.894100/-0.408900/15475; FF TNS/WNS/NVP -1.509000/-0.106500/61; area 7010.982832.
- Iteration 14: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -822.289100/-0.408900/15431; FF TNS/WNS/NVP -1.509000/-0.106500/61; area 7011.236304.
- Iteration 15: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -817.872800/-0.408900/15391; FF TNS/WNS/NVP -1.509000/-0.106500/61; area 7011.685576.
- Iteration 16: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -814.347300/-0.413900/15357; FF TNS/WNS/NVP -1.530000/-0.106500/61; area 7012.987112.
- Iteration 17: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -811.161300/-0.413900/15298; FF TNS/WNS/NVP -1.540200/-0.106500/62; area 7013.113848.
- Iteration 18: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -807.044400/-0.413900/15248; FF TNS/WNS/NVP -1.540200/-0.106500/62; area 7013.240584.
- Iteration 19: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -804.333100/-0.413900/15226; FF TNS/WNS/NVP -1.540200/-0.106500/62; area 7013.494056.
- Iteration 20: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -803.684000/-0.413900/15217; FF TNS/WNS/NVP -1.540200/-0.106500/62; area 7013.747528.
- Iteration 21: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -803.146700/-0.413900/15208; FF TNS/WNS/NVP -1.540200/-0.106500/62; area 7014.001000.
- Iteration 22: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -802.751800/-0.413900/15199; FF TNS/WNS/NVP -1.540200/-0.106500/62; area 7014.254472.
- Iteration 23: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -802.377000/-0.413900/15190; FF TNS/WNS/NVP -1.540200/-0.106500/62; area 7014.507944.
- Iteration 24: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -802.051300/-0.413900/15181; FF TNS/WNS/NVP -1.540200/-0.106500/62; area 7014.761416.
- Iteration 25: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -801.531600/-0.413900/15173; FF TNS/WNS/NVP -1.540200/-0.106500/62; area 7015.014888.
- Iteration 26: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -801.044500/-0.413900/15165; FF TNS/WNS/NVP -1.540200/-0.106500/62; area 7015.268360.
- Iteration 27: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -800.565100/-0.413900/15157; FF TNS/WNS/NVP -1.540200/-0.106500/62; area 7015.521832.
- Iteration 28: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -800.146200/-0.413900/15149; FF TNS/WNS/NVP -1.540200/-0.106500/62; area 7015.775304.
- Iteration 29: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -797.995900/-0.413900/15135; FF TNS/WNS/NVP -1.530200/-0.106500/60; area 7016.489440.
- Iteration 30: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -794.095600/-0.413900/15126; FF TNS/WNS/NVP -1.559300/-0.106500/64; area 7016.811976.
- Iteration 31: accepted TNS/NVP cleanup move; evaluated 768; SS TNS/WNS/NVP -790.448200/-0.418900/15109; FF TNS/WNS/NVP -1.582300/-0.106500/66; area 7018.113512.
- Iteration 32: accepted TNS/NVP cleanup move; evaluated 528; SS TNS/WNS/NVP -790.005600/-0.418900/15100; FF TNS/WNS/NVP -1.582300/-0.106500/66; area 7018.366984.

## Phase: group/ancestor insertion
- Time window: 420.000000-500.000000 sec
- Iteration 0: accepted group/ancestor insertion move; evaluated 768; SS TNS/WNS/NVP -789.005600/-0.418900/15100; FF TNS/WNS/NVP -1.536800/-0.106500/64; area 7018.620456.
- Iteration 1: accepted group/ancestor insertion move; evaluated 768; SS TNS/WNS/NVP -788.005600/-0.418900/15100; FF TNS/WNS/NVP -1.536800/-0.106500/64; area 7018.873928.
- Iteration 2: accepted group/ancestor insertion move; evaluated 768; SS TNS/WNS/NVP -787.005600/-0.418900/15100; FF TNS/WNS/NVP -1.536800/-0.106500/64; area 7019.127400.
- Iteration 3: accepted group/ancestor insertion move; evaluated 768; SS TNS/WNS/NVP -786.035300/-0.418900/15099; FF TNS/WNS/NVP -1.536800/-0.106500/64; area 7019.380872.
- Iteration 4: accepted group/ancestor insertion move; evaluated 768; SS TNS/WNS/NVP -785.066200/-0.418900/15098; FF TNS/WNS/NVP -1.536800/-0.106500/64; area 7019.634344.
- Iteration 5: accepted group/ancestor insertion move; evaluated 768; SS TNS/WNS/NVP -784.118000/-0.418900/15095; FF TNS/WNS/NVP -1.536800/-0.106500/64; area 7019.887816.
- Iteration 6: accepted group/ancestor insertion move; evaluated 768; SS TNS/WNS/NVP -782.330100/-0.418900/15091; FF TNS/WNS/NVP -1.536800/-0.106500/64; area 7020.141288.
- Iteration 7: accepted group/ancestor insertion move; evaluated 422; SS TNS/WNS/NVP -781.388900/-0.418900/15088; FF TNS/WNS/NVP -1.536800/-0.106500/64; area 7020.394760.

## Phase: hold final repair
- Time window: 480.000000-540.000000 sec
- Iteration 0: accepted hold final repair move; evaluated 384; SS TNS/WNS/NVP -781.288900/-0.418900/15088; FF TNS/WNS/NVP -1.432600/-0.083000/63; area 7020.648232.
- Iteration 1: accepted hold final repair move; evaluated 384; SS TNS/WNS/NVP -781.188900/-0.418900/15088; FF TNS/WNS/NVP -1.394600/-0.080500/62; area 7020.901704.
- Iteration 2: accepted hold final repair move; evaluated 384; SS TNS/WNS/NVP -781.088900/-0.418900/15088; FF TNS/WNS/NVP -1.306200/-0.070400/61; area 7021.155176.
- Iteration 3: accepted hold final repair move; evaluated 384; SS TNS/WNS/NVP -780.988900/-0.418900/15088; FF TNS/WNS/NVP -1.252000/-0.069400/60; area 7021.408648.
- Iteration 4: accepted hold final repair move; evaluated 384; SS TNS/WNS/NVP -780.888900/-0.418900/15088; FF TNS/WNS/NVP -1.180900/-0.068700/58; area 7021.662120.
- Iteration 5: accepted hold final repair move; evaluated 384; SS TNS/WNS/NVP -780.852600/-0.418900/15087; FF TNS/WNS/NVP -1.105300/-0.063400/58; area 7021.915592.
- Iteration 6: accepted hold final repair move; evaluated 384; SS TNS/WNS/NVP -780.688200/-0.418900/15086; FF TNS/WNS/NVP -1.026200/-0.062100/57; area 7022.169064.
- Iteration 7: accepted hold final repair move; evaluated 335; SS TNS/WNS/NVP -780.509300/-0.418900/15085; FF TNS/WNS/NVP -0.984400/-0.061800/56; area 7022.422536.

## Phase: area recovery
- Time window: 540.000000-570.000000 sec
- Iteration 0: accepted area recovery move; evaluated 512; SS TNS/WNS/NVP -778.465300/-0.418900/15052; FF TNS/WNS/NVP -0.984400/-0.061800/56; area 7021.443536.
- Iteration 1: accepted area recovery move; evaluated 512; SS TNS/WNS/NVP -774.447100/-0.418900/15001; FF TNS/WNS/NVP -0.971300/-0.061800/55; area 7020.464536.
- Iteration 2: accepted area recovery move; evaluated 512; SS TNS/WNS/NVP -773.032400/-0.418900/14977; FF TNS/WNS/NVP -0.971300/-0.061800/55; area 7019.485536.
- Iteration 3: accepted area recovery move; evaluated 512; SS TNS/WNS/NVP -768.422100/-0.418900/14930; FF TNS/WNS/NVP -0.971300/-0.061800/55; area 7018.506536.
- Iteration 4: accepted area recovery move; evaluated 236; SS TNS/WNS/NVP -767.835300/-0.418900/14917; FF TNS/WNS/NVP -0.971300/-0.061800/55; area 7017.527536.

## Final
- Final best robust score: 0.196054
- Final SS TNS/WNS/NVP: -1079.608100/-0.384500/18110
- Final FF TNS/WNS/NVP: -2.232600/-0.106500/98
- Final area: 7003.564504
- Applied moves: 68; inserted buffers: 44; resize moves: 5
