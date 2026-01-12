import os
from datetime import datetime
from io import BytesIO

import matplotlib.pyplot as plt
import numpy as np
import requests
from dotenv import load_dotenv
from PIL import Image, ImageDraw
from slack_sdk import WebClient

load_dotenv()

data_dir = "./data"
os.makedirs(os.path.join(data_dir, "images"), exist_ok=True)


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
            file=image_path,
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
        response = requests.get(camera, timeout=10)
        response.raise_for_status()
        original_image = Image.open(BytesIO(response.content)).convert("RGB")
    except Exception as e:
        print(f"Error fetching image: {e}")
        return

    width, height = original_image.size
    input_roi_image = apply_roi_mask(
        original_image,
        [
            (0, 1536),
            (94, 489),
            (406, 179),
            (1727, 168),
            (2048, 908),
            (2048, 1536),
        ],
    )

    from sam3.model.sam3_image_processor import Sam3Processor
    from sam3.model_builder import build_sam3_image_model

    processor = Sam3Processor(build_sam3_image_model(device="cpu"), device="cpu")
    inference_state = processor.set_image(input_roi_image)
    output = processor.set_text_prompt(state=inference_state, prompt="Car")

    masks = output["masks"]
    np_masks = masks.cpu().numpy()

    car_count = len(np_masks)

    dpi = 100
    figsize = width / float(dpi), height / float(dpi)

    plt.figure(figsize=figsize)
    plt.imshow(original_image)
    ax = plt.gca()

    poly_x, poly_y = zip(*(roi_points + [roi_points[0]]))
    plt.plot(poly_x, poly_y, "r-", linewidth=2, label="ROI")

    for mask in np_masks:
        if mask.ndim == 3:
            mask = mask[0]
        show_mask(mask, ax)

    plt.axis("off")

    save_path = os.path.join(data_dir, "images", f"{timestamp_file}.jpg")
    plt.savefig(save_path, bbox_inches="tight", pad_inches=0, dpi=dpi)
    plt.close()

    log_file_path = os.path.join(data_dir, "log.txt")
    with open(log_file_path, "a", encoding="utf-8") as f:
        f.write(f"{timestamp_log} : {car_count}\n")

    print(f"result: {car_count} ({save_path})")
    send_message(car_count, save_path, timestamp_log)


if __name__ == "__main__":
    detect()
