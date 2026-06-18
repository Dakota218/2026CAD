# Optimization log

Priority order: setup timing first, then hold timing, then area.

## Baseline
- Existing optimizer status: it did not have fixed 0-2/2-6/6-8/8-9/9-9.5 minute phases; it used one mixed greedy loop.
- New schedule: 0-2 WNS repair, 2-6 TNS/NVP cleanup, 6-8 group/ancestor insertion, 8-9 hold final repair, 9-9.5 area recovery, then output best_tree.
- Baseline SS TNS/WNS/NVP: -98.697600/-0.302000/2157
- Baseline FF TNS/WNS/NVP: -0.103400/-0.029800/5
- Baseline area: 980.414032

## Phase: WNS repair
- Time window: 0.000000-70.000000 sec
- Iteration 0: accepted WNS repair sequence; evaluated 384; sequence length 1; SS TNS/WNS/NVP -98.397600/-0.259600/2157; FF TNS/WNS/NVP -0.075100/-0.029800/4; area 980.794240.
  - Updated best_tree.
- Iteration 1: accepted WNS repair sequence; evaluated 384; sequence length 1; SS TNS/WNS/NVP -97.770100/-0.252800/2151; FF TNS/WNS/NVP -0.075100/-0.029800/4; area 981.174448.
  - Updated best_tree.
- Iteration 2: accepted WNS repair sequence; evaluated 384; sequence length 1; SS TNS/WNS/NVP -97.525600/-0.231200/2150; FF TNS/WNS/NVP -0.075100/-0.029800/4; area 981.554656.
  - Updated best_tree.
- Iteration 3: accepted WNS repair sequence; evaluated 384; sequence length 1; SS TNS/WNS/NVP -96.680200/-0.227300/2141; FF TNS/WNS/NVP -0.075100/-0.029800/4; area 981.934864.
  - Updated best_tree.
- Iteration 4: accepted WNS repair sequence; evaluated 384; sequence length 1; SS TNS/WNS/NVP -96.530200/-0.224400/2141; FF TNS/WNS/NVP -0.045300/-0.020400/3; area 982.315072.
  - Updated best_tree.
- Iteration 5: accepted WNS repair sequence; evaluated 384; sequence length 1; SS TNS/WNS/NVP -95.570600/-0.219600/2134; FF TNS/WNS/NVP -0.045300/-0.020400/3; area 982.695280.
  - Updated best_tree.
- Iteration 6: accepted WNS repair sequence; evaluated 384; sequence length 1; SS TNS/WNS/NVP -95.420600/-0.216900/2134; FF TNS/WNS/NVP -0.045300/-0.020400/3; area 983.075488.
  - Updated best_tree.
- Iteration 7: accepted WNS repair sequence; evaluated 384; sequence length 1; SS TNS/WNS/NVP -94.889300/-0.215600/2130; FF TNS/WNS/NVP -0.045300/-0.020400/3; area 983.455696.
  - Updated best_tree.
- Iteration 8: accepted WNS repair sequence; evaluated 384; sequence length 1; SS TNS/WNS/NVP -94.024000/-0.207000/2124; FF TNS/WNS/NVP -0.045300/-0.020400/3; area 983.835904.
  - Updated best_tree.
- Iteration 9: accepted WNS repair sequence; evaluated 384; sequence length 1; SS TNS/WNS/NVP -93.234700/-0.206200/2119; FF TNS/WNS/NVP -0.045300/-0.020400/3; area 984.216112.
  - Updated best_tree.
- Iteration 10: accepted WNS repair sequence; evaluated 384; sequence length 1; SS TNS/WNS/NVP -92.824000/-0.202400/2114; FF TNS/WNS/NVP -0.045300/-0.020400/3; area 984.469584.
  - Updated best_tree.
- Iteration 11: accepted WNS repair sequence; evaluated 384; sequence length 1; SS TNS/WNS/NVP -92.674000/-0.202000/2114; FF TNS/WNS/NVP -0.045300/-0.020400/3; area 984.849792.
  - Updated best_tree.
- Iteration 12: accepted WNS repair sequence; evaluated 384; sequence length 1; SS TNS/WNS/NVP -92.040400/-0.201400/2108; FF TNS/WNS/NVP -0.045300/-0.020400/3; area 985.230000.
  - Updated best_tree.
- Iteration 13: accepted WNS repair sequence; evaluated 384; sequence length 1; SS TNS/WNS/NVP -91.649900/-0.201400/2102; FF TNS/WNS/NVP -0.045300/-0.020400/3; area 986.001808.
  - Updated best_tree.
- Iteration 14: accepted WNS repair sequence; evaluated 384; sequence length 1; SS TNS/WNS/NVP -90.823000/-0.201000/2095; FF TNS/WNS/NVP -0.045300/-0.020400/3; area 986.382016.
  - Updated best_tree.
- Iteration 15: accepted WNS repair sequence; evaluated 384; sequence length 1; SS TNS/WNS/NVP -90.564500/-0.198300/2094; FF TNS/WNS/NVP -0.036800/-0.020400/2; area 986.762224.
  - Updated best_tree.
- Iteration 16: accepted WNS repair sequence; evaluated 384; sequence length 1; SS TNS/WNS/NVP -89.918100/-0.195000/2087; FF TNS/WNS/NVP -0.036800/-0.020400/2; area 987.142432.
  - Updated best_tree.
- Iteration 17: accepted WNS repair sequence; evaluated 384; sequence length 1; SS TNS/WNS/NVP -88.788100/-0.192000/2081; FF TNS/WNS/NVP -0.036800/-0