import requests
from io import BytesIO
from PIL import Image, ImageDraw
import numpy as np
import matplotlib.pyplot as plt
from sam3.model_builder import build_sam3_image_model
from sam3.model.sam3_image_processor import Sam3Processor

camera = "http://10.24.59.100/capture"
result = "result.jpg"

model = build_sam3_image_model(device="cpu")
processor = Sam3Processor(model, device="cpu")

response = requests.get(camera)
response.raise_for_status()

original_image = Image.open(BytesIO(response.content)).convert("RGB")
width, height = original_image.size

roi_points = [
    (0, 1536),
    (59, 524),
    (458, 283),
    (1690, 410),
    (1999, 1141),
    (2048, 1536)
]

def apply_roi_mask(image, points):
    mask = Image.new("L", image.size, 0)
    draw = ImageDraw.Draw(mask)
    
    draw.polygon(points, fill=255)
    
    black_bg = Image.new("RGB", image.size, (0, 0, 0))
    
    roi_image = Image.composite(image, black_bg, mask)
    return roi_image

input_roi_image = apply_roi_mask(original_image, roi_points)

inference_state = processor.set_image(input_roi_image)
output = processor.set_text_prompt(state=inference_state, prompt="Car")

masks = output["masks"]

def show_mask(mask, ax):
    color = np.concatenate([np.random.random(3), np.array([0.6])], axis=0)
    h, w = mask.shape[-2:]
    mask_image = mask.reshape(h, w, 1) * color.reshape(1, 1, -1)
    ax.imshow(mask_image)

dpi = 100
figsize = width / float(dpi), height / float(dpi)

plt.figure(figsize=figsize)

plt.imshow(original_image) 
ax = plt.gca()

poly_x, poly_y = zip(*(roi_points + [roi_points[0]]))
plt.plot(poly_x, poly_y, 'r-', linewidth=2, label="ROI")

np_masks = masks.cpu().numpy()

for mask in np_masks:
    if mask.ndim == 3:
        mask = mask[0]
    
    show_mask(mask, ax)

plt.axis("off")

plt.savefig(result , bbox_inches="tight", pad_inches=0, dpi=dpi)
plt.close()

print(f"count: {len(np_masks)}")
