import {makeProject} from '@motion-canvas/core';

import shot01 from './scenes/shot01?scene';
import shot02 from './scenes/shot02?scene';
import shot03 from './scenes/shot03?scene';
import shot04 from './scenes/shot04?scene';

export default makeProject({
  name: 'camel-promo',
  scenes: [shot01, shot02, shot03, shot04],
});
