import os
import shutil
from datetime import datetime
from io import BytesIO

import matplotlib.pyplot as plt
import numpy as np
import requests
from dotenv import load_dotenv
from PIL import Image, ImageDraw
from slack_sdk import WebClient

import graph

load_dotenv()

data_dir = "./data"
os.makedirs(os.path.join(data_dir, "images"), exist_ok=True)


def filter_overlapping_masks(masks, iou_threshold=0.3):
    if len(masks) == 0:
        return masks

    areas = [np.sum(m) for m in masks]
    sorted_indices = np.argsort(areas)[::-1]

    keep_indices = []

    for i in sorted_indices:
        current_mask = masks[i]
        is_overlapping = False

        for kept_idx in keep_indices:
            kept_mask = masks[kept_idx]

            intersection = np.logical_and(current_mask, kept_mask).sum()
            union = np.logical_or(current_mask, kept_mask).sum()

            iou = intersection / union if union > 0 else 0

            if iou > iou_threshold:
                is_overlapping = True
                break

        if not is_overlapping:
            keep_indices.append(i)

    return masks[keep_indices]


def apply_roi_mask(image, points):
    mask = Image.new("L", image.size, 0)
    draw = ImageDraw.Draw(mask)

    draw.polygon(points, fill=255)

    black_bg = Image.new("RGB", image.size, (0, 0, 0))

    roi_image = Image.composite(image, black_bg, mask)
    return roi_image


def show_mask(mask, ax):
    color = np.concatenate([np.random.random(3), np.array([0.6])], axis=0)
    h, w = mask.shape[-2:]
    mask_image = mask.reshape(h, w, 1) * color.reshape(1, 1, -1)
    ax.imshow(mask_image)


def send_message(car_count, image_path, timestamp):
    client = WebClient(token=os.getenv("token"))

    try:
        client.files_upload_v2(
            channel=os.getenv("channel"),
            file_uploads=image_path,
            initial_comment=f"{timestamp} - 차량 {car_count} / 33 대 ({car_count / 33 * 100:.1f}%)",
        )

    # ignore all exceptions
    except Exception as e:
        print(f"Error sending message to Slack: {e}")


def detect():
    camera = os.getenv("camera")

    now = datetime.now()
    timestamp_log = now.strftime("%Y-%m-%d %H:%M")
    timestamp_file = now.strftime("%Y-%m-%d-%H-%M")

    try:
        # Grab a single frame from MJPEG stream
        response = requests.get(camera, stream=True, timeout=10)
        response.raise_for_status()

        bytes_buffer = b""
        for chunk in response.iter_content(chunk_size=1024):
            bytes_buffer += chunk
            start = bytes_buffer.find(b"\xff\xd8")  # JPEG start
            end = bytes_buffer.find(b"\xff\xd9")    # JPEG end
            if start != -1 and end != -1 and end > start:
                jpg_data = bytes_buffer[start : end + 2]
                original_image = Image.open(BytesIO(jpg_data)).convert("RGB")
                response.close()
                break
        else:
            print("Error: Could not extract frame from MJPEG stream")
            return
    except Exception as e:
        print(f"Error fetching image: {e}")
        return

    width, height = original_image.size
    roi_points = [
        (0, 1024),
        (40, 469),
        (256, 263),
        (1064, 203),
        (1278, 533),
        (1280, 1024),
    ]
    input_roi_image = apply_roi_mask(original_image, roi_points)

    from ultralytics.models.sam import SAM3SemanticPredictor

    overrides = dict(
        conf=0.25,
        task="segment",
        mode="predict",
        model="sam3.pt",
        save=False,
        device="cpu",
    )

    predictor = SAM3SemanticPredictor(overrides=overrides)
    predictor.set_image(np.array(input_roi_image))
    results = predictor(text=["Car"])

    if results[0].masks is not None:
        np_masks = results[0].masks.data.cpu().numpy()
    else:
        np_masks = np.array([])

    if len(np_masks) > 0:
        if np_masks.ndim == 4:
            np_masks = np_masks.squeeze(1)
        np_masks = filter_overlapping_masks(np_masks, iou_threshold=0.15)
    else:
        np_masks = np.array([])

    car_count = len(np_masks)

    dpi = 100
    figsize = width / float(dpi), height / float(dpi)

    plt.figure(figsize=figsize)
    plt.imshow(original_image)
    ax = plt.gca()

    poly_x, poly_y = zip(*(roi_points + [roi_points[0]]))
    plt.plot(poly_x, poly_y, "r-", linewidth=2, label="ROI")

    for i, mask in enumerate(np_masks):
        if mask.ndim == 3:
            mask = mask[0]
        show_mask(mask, ax)

        y_indices, x_indices = np.where(mask)

        if len(x_indices) > 0 and len(y_indices) > 0:
            center_x = np.mean(x_indices)
            center_y = np.mean(y_indices)

            ax.text(
                center_x,
                center_y,
                str(i + 1),
                color="white",
                fontsize=12,
                fontweight="bold",
                ha="center",
                va="center",
                bbox=dict(
                    facecolor="black",
                    alpha=0.5,
                    edgecolor="none",
                    boxstyle="round,pad=0.2",
                ),
            )
        else:
            print(f"Warning: Empty mask for car {i + 1}, skipping annotation.")

    plt.axis("off")

    save_path = os.path.join(data_dir, "images", f"{timestamp_file}.jpg")
    plt.savefig(save_path, bbox_inches="tight", pad_inches=0, dpi=dpi)
    plt.close()

    original_save_path = os.path.join(data_dir, "images", f"{timestamp_file}-original.jpg")
    original_image.save(original_save_path)

    log_file_path = os.path.join(data_dir, "log.txt")

    with open(log_file_path, "a", encoding="utf-8") as f:
        f.write(f"{timestamp_log} : {car_count}\n")

    print(f"result: {car_count} ({save_path})")
    send_message(car_count, [{"file": save_path}, {"file": original_save_path}], timestamp_log)
    graph.generate_dashboard(log_file_path, "data/report.html", "data/report.png")

    # Save current car count to data/current
    current_file_path = os.path.join(data_dir, "current")
    with open(current_file_path, "w", encoding="utf-8") as f:
        f.write(str(car_count))

    # Save current image to data/images/current.jpg
    current_image_path = os.path.join(data_dir, "images", "current.jpg")
    shutil.copy(save_path, current_image_path)

    # Send webhook notification
    webhook_url = os.getenv("webhook")
    if webhook_url:
        try:
            requests.post(webhook_url, json={"count": car_count}, timeout=5)
        except Exception as e:
            print(f"Error sending webhook: {e}")


if __name__ == "__main__":
    detect()
