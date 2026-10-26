// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

import {makeProject} from '@motion-canvas/core';

import './fonts/fonts.css';

import shot00 from './scenes/shot00?scene';
import shot01 from './scenes/shot01?scene';
import shot02 from './scenes/shot02?scene';
import shot03 from './scenes/shot03?scene';
import shot04 from './scenes/shot04?scene';
import shot05 from './scenes/shot05?scene';
import shot06 from './scenes/shot06?scene';
import shot07 from './scenes/shot07?scene';
import shot08 from './scenes/shot08?scene';
import shot09 from './scenes/shot09?scene';
import shot10 from './scenes/shot10?scene';

const all = {shot00, shot01, shot02, shot03, shot04, shot05, shot06, shot07, shot08, shot09, shot10};

// `VITE_SCENES=shot06,shot07 npm run serve` loads only those scenes, so a render makes only them.
const only = (import.meta.env.VITE_SCENES as string | undefined)?.split(',').map((s) => s.trim()) as (keyof typeof all)[] | undefined;

export default makeProject({
  name: 'camel-promo',
  scenes: only ? only.map((name) => all[name]) : Object.values(all),
});
