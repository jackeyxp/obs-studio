import { link } from '../mixins/link';
import { VantComponent } from '../common/component';
VantComponent({
  classes: ['num-class', 'desc-class', 'time-class', 'age-class', 'thumb-class', 'title-class', 'price-class', 'origin-price-class'],
  mixins: [link],
  props: {
    tag: String,
    num: String,
    desc: String,
    thumb: String,
    title: String,
    price: String,
    cardAge: String,
    cardTime: String,
    centered: Boolean,
    lazyLoad: Boolean,
    shopCart: Boolean,
    showArrow: Boolean,
    originPrice: String,
    customStyle: String,
    thumbMode: {
      type: String,
      value: 'aspectFit'
    },
    currency: {
      type: String,
      value: '¥'
    }
  },
  methods: {
    onTap() {
      this.triggerEvent('click', this.data)
    }
  }
});