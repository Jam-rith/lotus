# Training Learned Ordering and Importance Models for APA

本文档说明如何为 APA 训练动态 state-elimination ordering model：

- `linear`: 用于动态 state-elimination ordering。它运行在每一步消除决策的热路径上。

动态 solver 的 `--elim-order learned-cost --order-model <model.json>` 只加载
linear JSON。

## 1. 整体流程

完整流程分为四步：

1. 用已有 ordering policy 运行 APA，并记录每一步候选节点的特征。
2. 用 `scripts/apa_train_order_model.py` 从 trace 中训练 linear ordering model。
3. 用 `--elim-order learned-cost` 加载 linear 模型，重新运行 APA。
4. 用整轮运行的真实时间和内存评价模型，而不是只看训练 loss。

核心文件如下：

- 训练脚本：`scripts/apa_train_order_model.py`
- 动态线性推理器：`include/Dataflow/APA/Importance/Dynamic/LinearOrderingModel.h`
- 通用特征定义：`include/Dataflow/APA/Importance/Dynamic/OrderingFeatures.h`
- 增量小根堆：`include/Dataflow/APA/Importance/Dynamic/IncrementalOrderingHeap.h`
- trace 输出：`include/Dataflow/APA/Importance/Dynamic/StateElimination/OrderTrace.h`
- solver 接入：`include/Dataflow/APA/Solver/StateEliminationSolver.h`

## 2. 生成训练数据

先选择一个基础 ordering policy 来生成 trace。建议从 `min-pred-succ` 开始，
因为它便宜、稳定，能作为第一个 teacher/baseline。

```bash
./build/bin/lotus-dfa-apa benchmarks/spec2006/998.specrand.bc \
  --analysis liveness \
  --elim-method state \
  --elim-order min-pred-succ \
  --order-trace-out /tmp/apa_order_trace.tsv \
  --order-run-summary-out /tmp/apa_order_summary.tsv
```

也可以替换成更丰富的 baseline：

```bash
--elim-order original
--elim-order expression-aware
--elim-order star-risk
```

`order_trace.tsv` 是训练样本来源。每一步会为所有 alive candidate 输出一行，
其中 `chosen=1` 的行是该 policy 实际选择消除的节点，并带有局部 proxy label。

常见特征包括：

- `alive_pred_count`, `alive_succ_count`, `pred_succ_product`
- `cross_combination_count`, `fill_in_count`, `existing_target_count`
- `self_*`, `incoming_*`, `outgoing_*` 路径表达式统计量
- 可选的 `matrix_*` 全局表达式统计量

局部 proxy labels 包括：

- `elapsed_us_for_chosen`
- `delta_total_unique_expr_nodes`
- `delta_total_concat_count`
- `delta_total_union_count`
- `delta_total_star_count`
- `affected_nodes`

这些 label 只用于训练启动和特征调试。真正评价 ordering 函数时，应看整轮
`order_run_summary.tsv` 中的真实 `elapsed_us` 和 `peak_rss_kb`。

## 3. 训练 linear ordering model

linear 模型适合作为第一版对照，因为它更容易解释每个特征的方向和权重。

```bash
python3 scripts/apa_train_order_model.py /tmp/apa_order_trace.tsv \
  --model linear \
  --out /tmp/apa_order_linear.json
```

默认 target 是 `elapsed_us_for_chosen`，并默认使用 `log1p` 变换，避免少数极慢
样本支配训练。

如需换成表达式增长 label：

```bash
python3 scripts/apa_train_order_model.py /tmp/apa_order_trace.tsv \
  --model linear \
  --target delta_total_unique_expr_nodes \
  --out /tmp/apa_order_linear_expr_growth.json
```

## 4. 关于 `matrix_*` 全局特征

训练脚本默认只使用局部 candidate features。这是有意的：局部特征只会在消除
某个节点后影响一小部分候选点，因此 learned ordering 可以用增量小根堆维护，
只刷新 affected candidates。

`matrix_*` 全局特征描述整个中间矩阵，例如：

- `matrix_total_unique_expr_nodes_before`
- `matrix_total_concat_count_before`
- `matrix_total_union_count_before`
- `matrix_total_star_count_before`
- `matrix_max_depth_before`

这些特征可能提升离线拟合，但每次消除后所有候选点的输入都会变化，因此在线
solver 必须刷新全部 alive candidates。只有在明确想做离线实验时才建议打开：

```bash
python3 scripts/apa_train_order_model.py /tmp/apa_order_trace.tsv \
  --model linear \
  --include-matrix-features \
  --out /tmp/apa_order_linear_with_matrix.json
```

## 5. 使用训练好的 ordering model

训练完成 linear 模型后，用 `learned-cost` 运行：

```bash
./build/bin/lotus-dfa-apa benchmarks/spec2006/998.specrand.bc \
  --analysis liveness \
  --elim-method state \
  --elim-order learned-cost \
  --order-model /tmp/apa_order_linear.json \
  --order-run-summary-out /tmp/apa_learned_summary.tsv
```

如果模型文件加载失败，`learned-cost` 当前会退回结构分数路径，因此实验时应检查
summary 中的 `requested_order` 和 `executed_order`，并保留命令输出/测试日志。

## 6. 评价 ordering 函数

不要只看训练脚本输出的 `validation_rmse_transformed`。它只说明模型是否能拟合
局部 proxy label，不能证明排序函数真的更快。

建议对同一组 bitcode、同一 analysis 跑以下 policy：

```bash
for order in original min-pred-succ expression-aware star-risk learned-cost; do
  ./build/bin/lotus-dfa-apa benchmarks/spec2006/998.specrand.bc \
    --analysis liveness \
    --elim-method state \
    --elim-order ${order} \
    --order-model /tmp/apa_order_linear.json \
    --order-run-summary-out /tmp/apa_compare.tsv
done
```

评价时主要看：

- `elapsed_us`: 整个函数分析真实耗时。
- `peak_rss_kb`: 进程峰值内存。
- `expr_total_unique_nodes`: 最终路径表达式 DAG 规模。
- `expr_total_concat_count`, `expr_total_union_count`, `expr_total_star_count`
- `expr_max_depth`: 表达式深度，能反映潜在解释/求值成本。

建议按 benchmark/function 聚合，比较 learned-cost 相对 baseline 的比例：

- speedup: `baseline_elapsed_us / learned_elapsed_us`
- memory ratio: `learned_peak_rss_kb / baseline_peak_rss_kb`
- expression reduction: `1 - learned_expr_nodes / baseline_expr_nodes`

## 7. 推荐实验组织方式

建议把数据分成三类：

- train: 用来生成 trace 和训练模型。
- validation: 用来调 `hidden-dims`, `epochs`, `target`, feature set。
- test: 最后只跑一次，用真实 runtime/memory 报告效果。

不要在 test 上反复调参。否则模型可能只是在适配某几个 SPEC 函数，而不是学到
可泛化的 ordering 规则。

一个最小但比较干净的实验表可以包含：

```text
benchmark  function  analysis  order  elapsed_us  peak_rss_kb
expr_total_unique_nodes  expr_total_concat_count  expr_total_union_count
expr_total_star_count  expr_max_depth
```

## 8. 当前限制

当前 learned ordering 只改变 `StateElimination`。ADT/ADTDelayed 默认执行论文中
的结构递归顺序，不再挂接 dynamic importance 统计或 tree-level ordering。

因此目前建议：

- 用 `--elim-method state` 研究 learned ordering 的有效性。
- ADT/ADTDelayed 如果后续要优化，需要重新设计合法的 tree-level
  ordering/splitting policy。

## 9. 常见问题

模型越复杂越好吗？

目前动态 ordering 只使用 linear model。先保证特征、label 和真实 runtime/memory
评价闭环稳定，再考虑更复杂模型。

为什么默认不用 `matrix_*`？

因为它会让每个 step 的所有候选点分数都变化，破坏“只刷新 affected candidates”
的增量堆优势。要用可以显式加 `--include-matrix-features`。

应该用哪个 target？

优先从 `elapsed_us_for_chosen` 开始。如果目标是控制表达式爆炸，可以试
`delta_total_unique_expr_nodes` 或 `delta_total_concat_count`。最终结论仍必须用
整轮 `elapsed_us` 和 `peak_rss_kb` 支撑。

训练数据越多越好吗？

通常是，但要注意多样性。只用一个 benchmark 训练出的模型可能会过拟合它的 CFG
形状；更好的做法是混合不同规模、不同分析、不同 CFG 结构的数据。
