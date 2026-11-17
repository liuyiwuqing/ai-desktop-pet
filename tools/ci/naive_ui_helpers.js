// Exercise Naive controls through their rendered DOM and keyboard behavior, never component state.
function createUiHelpers({ js, wait }) {
  async function choose(selector, label) {
    let opened = false;
    for (let attempt = 0; attempt < 100; attempt++) {
      opened = await js(`(() => {
        const root = document.querySelector(${JSON.stringify(selector)});
        const selection = root?.matches('.n-base-selection') ? root : root?.querySelector('.n-base-selection');
        if (!selection || selection.classList.contains('n-base-selection--disabled')) return false;
        selection.click(); selection.querySelector('[tabindex="0"]')?.focus(); return true;
      })()`);
      if (opened) break;
      await wait(.1);
    }
    if (!opened) throw new Error('Select unavailable: ' + selector);
    for (let attempt = 0; attempt < 100; attempt++) {
      const found = await js(`(() => {
        const option = [...document.querySelectorAll('.n-base-select-option')].find(e =>
          e.textContent.trim() === ${JSON.stringify(label)} && !e.classList.contains('n-base-select-option--disabled') && e.getBoundingClientRect().height > 0);
        if (!option) return false;
        option.click(); return true;
      })()`);
      if (found) { await js(`document.querySelector(${JSON.stringify(selector)})?.querySelector('.n-base-selection--active [tabindex="0"]')?.dispatchEvent(new KeyboardEvent('keydown',{key:'Escape',bubbles:true}))`); return; }
      await wait(.1);
    }
    throw new Error('Select option unavailable: ' + label + ' in ' + selector);
  }
  async function optionLabels(selector, minimumCount = 1) {
    await js(`document.querySelector(${JSON.stringify(selector)}).querySelector('.n-base-selection').click()`);
    for (let attempt = 0; attempt < 100; attempt++) {
      const labels = await js("[...document.querySelectorAll('.n-base-select-option')].filter(e=>e.getBoundingClientRect().height>0).map(e=>e.textContent.trim())");
      if (labels.length >= minimumCount) {
        await js(`document.querySelector(${JSON.stringify(selector)}).querySelector('.n-base-selection').dispatchEvent(new KeyboardEvent('keydown',{key:'Escape',bubbles:true}))`);
        return labels;
      }
      await wait(.1);
    }
    throw new Error('Select menu has no visible options: ' + selector);
  }
  return { choose, optionLabels };
}

const ui = createUiHelpers({ js, wait });
