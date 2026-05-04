"""
MNIST visualization for Camel - loss curve and sample predictions.
Call from Camel via py_call with wrapped arrays.
"""


def visualize(loss_history: list, images_flat: list, n: int, d: int,
              labels: list, pred: list, show_window: bool = True) -> None:
    """
    Plot training loss curve and sample predictions.
    - loss_history: list of loss values (every 100 iters)
    - images_flat: flat float list, length n*d
    - n, d: shape (n samples, d=784 pixels)
    - labels: true labels (int)
    - pred: predicted labels (int)
    - show_window: True = 窗口展示, False = 保存为图片
    """
    try:
        import matplotlib
        if not show_window:
            matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib not installed, skipping visualization")
        return

    # 合并为一个图：上方损失曲线，下方 2x5 样本预测
    has_loss = bool(loss_history)
    has_samples = bool(images_flat and pred and labels)
    n_rows = 3 if (has_loss and has_samples) else (2 if has_samples else 1)

    fig = plt.figure(figsize=(10, 2.5 * n_rows))
    gs = fig.add_gridspec(n_rows, 5, hspace=0.35, wspace=0.1)
    fig.suptitle("MNIST Softmax Training", fontsize=14)

    # 1. Loss curve
    row = 0
    if loss_history:
        ax_loss = fig.add_subplot(gs[0, :])
        iters = list(range(0, len(loss_history) * 100, 100))[:len(loss_history)]
        ax_loss.plot(iters, loss_history, "b-", linewidth=1.5)
        ax_loss.set_xlabel("Iteration")
        ax_loss.set_ylabel("Cross-Entropy Loss")
        ax_loss.grid(True, alpha=0.3)
        row = 1

    # 2. Sample predictions (2x5 grid)
    if images_flat and pred and labels:
        def safe_int(x):
            if x is None:
                return 0
            return int(float(x)) if isinstance(x, (int, float)) else int(x)

        pred_arr = [safe_int(x) for x in pred]
        labels_arr = [safe_int(x) for x in labels[:n]]
        sample_count = min(10, n, len(pred_arr), len(labels_arr))

        for idx in range(sample_count):
            r, c = row + idx // 5, idx % 5
            ax = fig.add_subplot(gs[r, c])
            image = images_flat[idx * d : (idx + 1) * d]
            image_2d = [image[start : start + 28] for start in range(0, min(len(image), 28 * 28), 28)]
            ax.imshow(image_2d, cmap="gray")
            true_lbl = labels_arr[idx]
            pred_lbl = pred_arr[idx]
            color = "green" if true_lbl == pred_lbl else "red"
            ax.set_title(f"True:{true_lbl} Pred:{pred_lbl}", color=color, fontsize=10)
            ax.axis("off")

    fig.tight_layout()
    if show_window:
        plt.show()
    else:
        fig.savefig("test/tmp/mnist/mnist_result.png")
        plt.close(fig)
        print("Saved to test/tmp/mnist/mnist_result.png")
