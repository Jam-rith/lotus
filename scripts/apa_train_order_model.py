#!/usr/bin/env python3
"""Train APA importance/order cost models.

The default model is a standardized ridge-linear regressor for dynamic
elimination ordering. Pass --model mlp only for one-shot/static importance
experiments; dynamic online ordering intentionally loads linear models only.
Full-run runtime/memory should still be evaluated with order_run_summary.tsv;
per-step labels are only a bootstrapping signal.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import random
import statistics
from pathlib import Path
from typing import Dict, Iterable, List, Sequence, Tuple


DEFAULT_LOCAL_FEATURES = [
    "score",
    "alive_pred_count",
    "alive_succ_count",
    "pred_succ_product",
    "cross_combination_count",
    "fill_in_count",
    "existing_target_count",
    "existing_via_concat_count",
    "shared_input_ref_count",
    "self_unique_nodes",
    "self_union_count",
    "self_concat_count",
    "self_star_count",
    "self_max_depth",
    "incoming_unique_nodes",
    "incoming_union_count",
    "incoming_concat_count",
    "incoming_star_count",
    "incoming_max_depth",
    "outgoing_unique_nodes",
    "outgoing_union_count",
    "outgoing_concat_count",
    "outgoing_star_count",
    "outgoing_max_depth",
]

# Matrix-wide features are useful for offline experiments, but they change
# after every elimination step. Keeping them out of the default model lets the
# online solver maintain a true incremental heap and refresh only affected
# candidates.
MATRIX_FEATURES = [
    "matrix_total_unique_expr_nodes_before",
    "matrix_total_shared_refs_before",
    "matrix_total_concat_count_before",
    "matrix_total_union_count_before",
    "matrix_total_star_count_before",
    "matrix_max_depth_before",
]

DEFAULT_FEATURES = DEFAULT_LOCAL_FEATURES


def parse_number(value: str) -> float:
    if value is None or value == "":
        return 0.0
    try:
        parsed = float(value)
    except ValueError:
        return 0.0
    if not math.isfinite(parsed):
        return 0.0
    return parsed


def load_chosen_rows(path: Path, target: str) -> List[Dict[str, str]]:
    rows: List[Dict[str, str]] = []
    with path.open(newline="") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        for row in reader:
            if row.get("chosen") != "1":
                continue
            if target not in row:
                raise SystemExit(f"target column '{target}' is missing")
            rows.append(row)
    return rows


def vectorize(
    rows: Sequence[Dict[str, str]], features: Sequence[str], target: str
) -> Tuple[List[List[float]], List[float]]:
    xs: List[List[float]] = []
    ys: List[float] = []
    for row in rows:
        xs.append([parse_number(row.get(feature, "")) for feature in features])
        ys.append(parse_number(row.get(target, "")))
    return xs, ys


def standardize(
    xs: Sequence[Sequence[float]],
) -> Tuple[List[List[float]], List[float], List[float]]:
    if not xs:
        return [], [], []
    columns = list(zip(*xs))
    means = [statistics.fmean(column) for column in columns]
    stds = []
    for column, mean in zip(columns, means):
        variance = statistics.fmean((value - mean) ** 2 for value in column)
        stds.append(math.sqrt(variance) if variance > 1e-12 else 1.0)
    scaled = [
        [(value - means[index]) / stds[index] for index, value in enumerate(row)]
        for row in xs
    ]
    return scaled, means, stds


def transform_target(ys: Sequence[float], mode: str) -> List[float]:
    if mode == "identity":
        return list(ys)
    if mode == "log1p":
        return [math.log1p(max(0.0, value)) for value in ys]
    raise SystemExit(f"unknown target transform: {mode}")


def solve_linear_system(matrix: List[List[float]], rhs: List[float]) -> List[float]:
    """Gauss-Jordan solve with tiny diagonal damping for numerical rough edges."""

    n = len(rhs)
    aug = [row[:] + [rhs_value] for row, rhs_value in zip(matrix, rhs)]
    for col in range(n):
        pivot = max(range(col, n), key=lambda row: abs(aug[row][col]))
        if abs(aug[pivot][col]) < 1e-12:
            aug[col][col] += 1e-8
            pivot = col
        aug[col], aug[pivot] = aug[pivot], aug[col]

        scale = aug[col][col]
        if abs(scale) < 1e-12:
            scale = 1e-8
            aug[col][col] = scale
        for j in range(col, n + 1):
            aug[col][j] /= scale

        for row in range(n):
            if row == col:
                continue
            factor = aug[row][col]
            if factor == 0.0:
                continue
            for j in range(col, n + 1):
                aug[row][j] -= factor * aug[col][j]
    return [aug[row][n] for row in range(n)]


def fit_ridge(xs: Sequence[Sequence[float]], ys: Sequence[float], alpha: float) -> List[float]:
    if not xs:
        return []
    n_features = len(xs[0])
    # Add one unregularized intercept column at the front.
    dim = n_features + 1
    xtx = [[0.0 for _ in range(dim)] for _ in range(dim)]
    xty = [0.0 for _ in range(dim)]

    for row, y in zip(xs, ys):
        full = [1.0] + list(row)
        for i in range(dim):
            xty[i] += full[i] * y
            for j in range(dim):
                xtx[i][j] += full[i] * full[j]

    for i in range(1, dim):
        xtx[i][i] += alpha
    return solve_linear_system(xtx, xty)


def predict(weights: Sequence[float], row: Sequence[float]) -> float:
    return weights[0] + sum(w * x for w, x in zip(weights[1:], row))


def relu(value: float) -> float:
    return value if value > 0.0 else 0.0


def mlp_forward(layers: Sequence[Dict[str, object]], row: Sequence[float]) -> Tuple[List[List[float]], List[List[float]]]:
    activations: List[List[float]] = [list(row)]
    preactivations: List[List[float]] = []
    current = list(row)
    for layer_index, layer in enumerate(layers):
        input_dim = int(layer["input_dim"])
        output_dim = int(layer["output_dim"])
        weights = layer["weights"]  # row-major [out][in]
        bias = layer["bias"]
        if len(current) != input_dim:
            raise ValueError("MLP layer input dimension mismatch")

        z_values: List[float] = []
        next_values: List[float] = []
        for out in range(output_dim):
            value = float(bias[out])
            base = out * input_dim
            for inp in range(input_dim):
                value += current[inp] * float(weights[base + inp])
            z_values.append(value)
            next_values.append(value if layer_index + 1 == len(layers) else relu(value))
        preactivations.append(z_values)
        activations.append(next_values)
        current = next_values
    return activations, preactivations


def mlp_predict(layers: Sequence[Dict[str, object]], row: Sequence[float]) -> float:
    activations, _ = mlp_forward(layers, row)
    return activations[-1][0] if activations and activations[-1] else 0.0


def init_mlp_layers(input_dim: int, hidden_dims: Sequence[int], seed: int) -> List[Dict[str, object]]:
    rng = random.Random(seed)
    dims = [input_dim] + list(hidden_dims) + [1]
    layers: List[Dict[str, object]] = []
    for input_size, output_size in zip(dims, dims[1:]):
        scale = math.sqrt(2.0 / max(1, input_size))
        weights = [rng.gauss(0.0, scale) for _ in range(input_size * output_size)]
        bias = [0.0 for _ in range(output_size)]
        layers.append(
            {
                "input_dim": input_size,
                "output_dim": output_size,
                "weights": weights,
                "bias": bias,
            }
        )
    return layers


def train_mlp(
    xs: Sequence[Sequence[float]],
    ys: Sequence[float],
    hidden_dims: Sequence[int],
    epochs: int,
    learning_rate: float,
    weight_decay: float,
    batch_size: int,
    seed: int,
) -> List[Dict[str, object]]:
    if not xs:
        return []
    layers = init_mlp_layers(len(xs[0]), hidden_dims, seed)
    rng = random.Random(seed)
    indexes = list(range(len(xs)))
    batch_size = max(1, batch_size)

    for _epoch in range(max(1, epochs)):
        rng.shuffle(indexes)
        for offset in range(0, len(indexes), batch_size):
            batch = indexes[offset : offset + batch_size]
            grad_w = [[0.0 for _ in layer["weights"]] for layer in layers]
            grad_b = [[0.0 for _ in layer["bias"]] for layer in layers]

            for index in batch:
                activations, preactivations = mlp_forward(layers, xs[index])
                prediction = activations[-1][0]
                # d/dprediction (prediction - y)^2
                delta = [2.0 * (prediction - ys[index])]

                for layer_index in reversed(range(len(layers))):
                    layer = layers[layer_index]
                    input_dim = int(layer["input_dim"])
                    output_dim = int(layer["output_dim"])
                    prev_activation = activations[layer_index]
                    next_delta = [0.0 for _ in range(input_dim)]

                    for out in range(output_dim):
                        d_out = delta[out]
                        if layer_index + 1 != len(layers) and preactivations[layer_index][out] <= 0.0:
                            d_out = 0.0
                        grad_b[layer_index][out] += d_out
                        base = out * input_dim
                        for inp in range(input_dim):
                            grad_w[layer_index][base + inp] += d_out * prev_activation[inp]
                            next_delta[inp] += d_out * float(layer["weights"][base + inp])
                    delta = next_delta

            inv_batch = 1.0 / len(batch)
            for layer_index, layer in enumerate(layers):
                weights = layer["weights"]
                bias = layer["bias"]
                for i in range(len(weights)):
                    weights[i] -= learning_rate * (
                        grad_w[layer_index][i] * inv_batch + weight_decay * float(weights[i])
                    )
                for i in range(len(bias)):
                    bias[i] -= learning_rate * grad_b[layer_index][i] * inv_batch
    return layers


def rmse(predictions: Iterable[float], labels: Iterable[float]) -> float:
    errors = [(p - y) ** 2 for p, y in zip(predictions, labels)]
    if not errors:
        return 0.0
    return math.sqrt(statistics.fmean(errors))


def train_validation_split(
    xs: List[List[float]], ys: List[float], validation_ratio: float, seed: int
) -> Tuple[List[List[float]], List[float], List[List[float]], List[float]]:
    indexes = list(range(len(xs)))
    random.Random(seed).shuffle(indexes)
    n_valid = max(1, int(len(indexes) * validation_ratio)) if len(indexes) >= 5 else 0
    valid_indexes = set(indexes[:n_valid])
    train_xs, train_ys, valid_xs, valid_ys = [], [], [], []
    for index, (row, y) in enumerate(zip(xs, ys)):
        if index in valid_indexes:
            valid_xs.append(row)
            valid_ys.append(y)
        else:
            train_xs.append(row)
            train_ys.append(y)
    return train_xs, train_ys, valid_xs, valid_ys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path, help="order_trace.tsv from lotus-dfa-apa")
    parser.add_argument("--out", type=Path, default=Path("apa_order_model.json"))
    parser.add_argument("--target", default="elapsed_us_for_chosen")
    parser.add_argument("--features", nargs="*", default=DEFAULT_FEATURES)
    parser.add_argument(
        "--include-matrix-features",
        action="store_true",
        help=(
            "Append matrix-wide expression features. This can improve offline "
            "fit, but it is best suited to one-shot/static experiments because "
            "online dynamic ordering would have to refresh all alive candidates "
            "after each elimination step."
        ),
    )
    parser.add_argument("--alpha", type=float, default=1.0, help="ridge strength")
    parser.add_argument("--model", choices=["linear", "mlp"], default="linear")
    parser.add_argument(
        "--hidden-dims",
        type=int,
        nargs="*",
        default=[16, 8],
        help="MLP hidden layer sizes",
    )
    parser.add_argument("--epochs", type=int, default=200, help="MLP training epochs")
    parser.add_argument("--learning-rate", type=float, default=0.01, help="MLP SGD learning rate")
    parser.add_argument("--weight-decay", type=float, default=1e-4, help="MLP L2 weight decay")
    parser.add_argument("--batch-size", type=int, default=32, help="MLP mini-batch size")
    parser.add_argument("--target-transform", choices=["log1p", "identity"], default="log1p")
    parser.add_argument("--validation-ratio", type=float, default=0.2)
    parser.add_argument("--seed", type=int, default=0)
    args = parser.parse_args()
    selected_features = list(args.features)
    if args.include_matrix_features:
        for feature in MATRIX_FEATURES:
            if feature not in selected_features:
                selected_features.append(feature)

    rows = load_chosen_rows(args.trace, args.target)
    if not rows:
        raise SystemExit("no chosen rows found; run with --order-trace-out first")

    xs_raw, ys_raw = vectorize(rows, selected_features, args.target)
    xs, means, stds = standardize(xs_raw)
    ys = transform_target(ys_raw, args.target_transform)
    train_xs, train_ys, valid_xs, valid_ys = train_validation_split(
        xs, ys, args.validation_ratio, args.seed
    )
    if not train_xs:
        train_xs, train_ys = xs, ys
        valid_xs, valid_ys = [], []

    if args.model == "linear":
        weights = fit_ridge(train_xs, train_ys, args.alpha)
        train_predictions = [predict(weights, row) for row in train_xs]
        valid_predictions = [predict(weights, row) for row in valid_xs]
        model = {
            "model_type": "standardized_ridge_linear_regression",
            "intercept": weights[0],
            "weights": dict(zip(selected_features, weights[1:])),
        }
    else:
        layers = train_mlp(
            train_xs,
            train_ys,
            args.hidden_dims,
            args.epochs,
            args.learning_rate,
            args.weight_decay,
            args.batch_size,
            args.seed,
        )
        train_predictions = [mlp_predict(layers, row) for row in train_xs]
        valid_predictions = [mlp_predict(layers, row) for row in valid_xs]
        model = {
            "model_type": "standardized_mlp_regressor",
            "hidden_dims": list(args.hidden_dims),
            "layers": layers,
        }

    model.update(
        {
            "purpose": "predict lower local elimination cost; use lower score first",
            "target": args.target,
            "target_transform": args.target_transform,
            "features": selected_features,
            "feature_mean": dict(zip(selected_features, means)),
            "feature_std": dict(zip(selected_features, stds)),
            "training_rows": len(train_xs),
            "validation_rows": len(valid_xs),
            "train_rmse_transformed": rmse(train_predictions, train_ys),
            "validation_rmse_transformed": rmse(valid_predictions, valid_ys),
            "notes": [
                "Rows come only from nodes actually chosen by the logging policy.",
                "Per-step labels are proxy labels; compare ordering policies with full-run elapsed_us and peak_rss_kb.",
                "Dynamic online ordering loads linear models only; MLP models are intended for one-shot/static importance scoring.",
            ],
        }
    )

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(model, indent=2, sort_keys=True) + "\n")
    print(
        f"trained {model['model_type']} on {len(train_xs)} rows; "
        f"validation rows={len(valid_xs)}; output={args.out}"
    )
    if valid_xs:
        print(f"validation_rmse_transformed={model['validation_rmse_transformed']:.6g}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
