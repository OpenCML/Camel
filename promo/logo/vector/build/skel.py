# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

import numpy as np, json
from PIL import Image
from scipy import ndimage
from skimage.morphology import skeletonize
S = 2.0                                   # skeleton raster: 2 px per 1024-frame unit
hi = np.array(Image.open("/work/promo/logo/camel.pbm").convert("L")) < 128      # 3677 px, the camel without the ring
img = Image.fromarray((hi * 255).astype(np.uint8)).resize((2048, 2048), Image.LANCZOS)
m = ndimage.gaussian_filter(np.array(img).astype(float) / 255, 1.2) > 0.5
dist = ndimage.distance_transform_edt(m) / S          # half-width in frame units
sk = skeletonize(m)
print("ink px", m.sum(), "skeleton px", sk.sum())
w = 2 * dist[sk]; print("stroke width along skeleton: p10 %.1f p25 %.1f median %.1f p75 %.1f p90 %.1f" % tuple(np.percentile(w, [10, 25, 50, 75, 90])))
np.save("sk.npy", sk); np.save("dist.npy", dist); np.save("mask.npy", m)
