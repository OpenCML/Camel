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

export default makeProject({
  name: 'camel-promo',
  scenes: [shot00, shot01, shot02, shot03, shot04, shot05, shot06, shot07, shot08, shot09, shot10],
});
