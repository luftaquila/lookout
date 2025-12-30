import argparse
import sys
from io import BytesIO

import requests
import numpy as np
import cv2

from ultralytics import YOLO

VEHICLE_LABELS = {"car", "truck", "bus", "motorcycle", "bicycle"}

def fetch_image_from_url(url: str, timeout: int = 10) -> np.ndarray:
    headers = {
        "User-Agent": "Mozilla/5.0 (car-counter/1.0)"
    }
    r = requests.get(url, headers=headers, timeout=timeout)
    r.raise_for_status()

    data = np.frombuffer(r.content, dtype=np.uint8)
    img = cv2.imdecode(data, cv2.IMREAD_COLOR)  # BGR
    if img is None:
        raise ValueError("다운로드한 데이터가 이미지로 디코딩되지 않습니다. (URL이 이미지가 아닐 수 있음)")
    return img


def enhance_contrast_bgr(img_bgr):
    lab = cv2.cvtColor(img_bgr, cv2.COLOR_BGR2LAB)
    l, a, b = cv2.split(lab)
    clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8, 8))
    l2 = clahe.apply(l)
    lab2 = cv2.merge([l2, a, b])
    return cv2.cvtColor(lab2, cv2.COLOR_LAB2BGR)


def count_vehicles(
    img_bgr: np.ndarray,
    model: YOLO,
    conf: float = 0.25,
    iou: float = 0.45,
    contrast: bool = False,
) -> tuple[int, dict]:
    img_rgb = cv2.cvtColor(enhance_contrast_bgr(img_bgr) if contrast else img_bgr, cv2.COLOR_BGR2RGB)

    results = model.predict(img_rgb, conf=conf, iou=iou, verbose=False)
    if not results:
        return 0, {}

    res = results[0]
    names = res.names

    counts = {k: 0 for k in VEHICLE_LABELS}

    if res.boxes is None or len(res.boxes) == 0:
        return 0, counts

    cls_ids = res.boxes.cls.cpu().numpy().astype(int)

    for cid in cls_ids:
        label = names.get(cid, str(cid))
        if label in VEHICLE_LABELS:
            counts[label] += 1

    total = sum(counts.values())
    return total, counts


def draw_detections(
    img_bgr: np.ndarray,
    model: YOLO,
    conf: float = 0.25,
    iou: float = 0.45,
    contrast: bool = False,
) -> np.ndarray:
    img_rgb = cv2.cvtColor(enhance_contrast_bgr(img_bgr) if contrast else img_bgr, cv2.COLOR_BGR2RGB)
    results = model.predict(img_rgb, conf=conf, iou=iou, verbose=False)

    if not results:
        return img_bgr

    plotted = results[0].plot(line_width=1)
    return cv2.cvtColor(plotted, cv2.COLOR_RGB2BGR)


def main():
    parser = argparse.ArgumentParser(description="URL 주차장 이미지에서 차량 수 세기(YOLO)")
    parser.add_argument("--url", required=True, help="주차장 이미지 URL (jpg/png 등 정지 이미지)")
    parser.add_argument("--model", default="yolo12x.pt", help="YOLO 모델")
    parser.add_argument("--conf", type=float, default=0.25, help="confidence threshold (기본: 0.25)")
    parser.add_argument("--iou", type=float, default=0.45, help="NMS IoU threshold (기본: 0.45)")
    parser.add_argument("--timeout", type=int, default=10, help="URL 요청 타임아웃(초)")
    parser.add_argument("--save-vis", default=None, help="탐지 시각화 이미지를 저장할 경로(예: out.jpg)")
    parser.add_argument("--contrast", default=None, help="컨트라스트 향상 이미지 사용")
    args = parser.parse_args()

    try:
        img = fetch_image_from_url(args.url, timeout=args.timeout)
    except Exception as e:
        print(f"[ERROR] 이미지 다운로드/디코딩 실패: {e}", file=sys.stderr)
        sys.exit(1)

    try:
        model = YOLO(args.model)
    except Exception as e:
        print(f"[ERROR] YOLO 모델 로드 실패: {e}", file=sys.stderr)
        sys.exit(1)

    total, counts = count_vehicles(img, model, conf=args.conf, iou=args.iou, contrast=args.contrast)

    print(f"총 차량 수: {total}")
    print("클래스별:")
    for k in sorted(counts.keys()):
        print(f"  {k}: {counts[k]}")

    if args.save_vis:
        vis = draw_detections(img, model, conf=args.conf, iou=args.iou, contrast=args.contrast)
        ok = cv2.imwrite(args.save_vis, vis)
        if ok:
            print(f"시각화 저장: {args.save_vis}")
        else:
            print(f"[WARN] 시각화 저장 실패: {args.save_vis}", file=sys.stderr)


if __name__ == "__main__":
    main()
