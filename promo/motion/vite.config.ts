// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

import {defineConfig} from 'vite';
import motionCanvas from '@motion-canvas/vite-plugin';
import ffmpeg from '@motion-canvas/ffmpeg';

export default defineConfig({
  plugins: [motionCanvas(), ffmpeg()],
});
