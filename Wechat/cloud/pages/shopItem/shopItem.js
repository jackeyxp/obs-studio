
import Notify from '../../vant-weapp/notify/notify';
import Dialog from '../../vant-weapp/dialog/dialog';

// 获取全局的app对象...
const g_app = getApp()

Page({
  /**
   * 页面的初始数据
   */
  data: {
    m_bEdit: false,
    m_curMasterName: '',
    m_curAgentName: '',
    m_curMasterID: 0,
    m_curAgentID: 0,
    m_arrMaster: [],
    m_arrAgent: [],
    m_arrArea: [],
    m_curArea: '',
    m_shopName: '',
    m_shopAddr: '',
    m_shopPhone: ''
  },

  /**
   * 生命周期函数--监听页面加载
   */
  onLoad: function (options) {
    // 显示导航栏|浮动加载动画...
    wx.showLoading({ title: '加载中' });
    // 保存this对象...
    let that = this
    let isEdit = parseInt(options.edit);
    // 构造访问接口连接地址...
    let theCurMasterID = (isEdit ? g_app.globalData.m_curSelectItem.master_id : 0);
    let theUrl = g_app.globalData.m_urlPrev + 'Mini/getMasterFree/master_id/' + theCurMasterID;
    // 请求远程API过程...
    wx.request({
      url: theUrl,
      method: 'GET',
      dataType: 'x-www-form-urlencoded',
      header: { 'content-type': 'application/x-www-form-urlencoded' },
      success: function (res) {
        // 隐藏导航栏加载动画...
        wx.hideLoading();
        // 调用接口失败...
        if (res.statusCode != 200) {
          Notify('获取空闲校长列表失败！');
          return;
        }
        // dataType 没有设置json，需要自己转换...
        let arrJson = JSON.parse(res.data);
        that.doShowShop(isEdit, arrJson.master, arrJson.agent);
      },
      fail: function (res) {
        // 隐藏导航栏加载动画...
        wx.hideLoading();
        Notify('获取空闲校长列表失败！');
      }
    });
  },
  // 显示具体的学校操作界面...
  doShowShop: function(isEdit, arrMaster, arrAgent) {
    let strTitle = (isEdit ? '修改' : '添加') + ' - 学校';
    let theShop  = isEdit ? g_app.globalData.m_curSelectItem : null;
    let theArrArea  = isEdit ? [theShop.province, theShop.city, theShop.area] : [];
    let theCurArea  = isEdit ? theArrArea.join(' / ') : '请选择';
    let theShopName = isEdit ? theShop.name : '';
    let theShopAddr = isEdit ? theShop.addr : '';
    let theShopPhone = isEdit ? theShop.phone : '';
    let theCurMasterID = (isEdit ? theShop.master_id : 0);
    let theCurMasterName = isEdit ? theShop.wx_nickname : '请选择';
    let theCurAgentID = (isEdit ? theShop.agent_id : 0);
    let theCurAgentName = isEdit ? theShop.agent_name : '请选择';
    let theCurMasterIndex = 0; let theCurAgentIndex = 0;
    // 修改标题信息...
    wx.setNavigationBarTitle({ title: strTitle });
    // 如果是编辑状态，需要找到校长所在的索引编号...
    if (isEdit && arrMaster.length > 0) {
      for (let index = 0; index < arrMaster.length; ++index) {
        if (arrMaster[index].user_id == theCurMasterID) {
          theCurMasterIndex = index;
          break;
        }
      }
    }
    // 如果是编辑状态，需要找到机构所在的索引编号...
    if (isEdit && arrAgent.length > 0) {
      for (let index = 0; index < arrAgent.length; ++index) {
        if (arrAgent[index].agent_id == theCurAgentID) {
          theCurAgentIndex = index;
          break;
        }
      }
    }
    // 应用到界面...
    this.setData({ 
      m_bEdit: isEdit, 
      m_arrArea: theArrArea, 
      m_curArea: theCurArea,
      m_shopName: theShopName, 
      m_shopAddr: theShopAddr,
      m_shopPhone: theShopPhone,
      m_arrMaster: arrMaster,
      m_curMasterID: theCurMasterID,
      m_curMasterName: theCurMasterName,
      m_curMasterIndex: theCurMasterIndex,
      m_arrAgent: arrAgent,
      m_curAgentID: theCurAgentID,
      m_curAgentName: theCurAgentName,
      m_curAgentIndex: theCurAgentIndex,
    });
  },
  // 点击取消按钮...
  doBtnCancel: function(event) {
    wx.navigateBack();
  },
  // 点击保存按钮...
  doBtnSave: function(event) {
    if (this.data.m_shopName.length <= 0) {
      Notify('【学校名称】不能为空，请重新输入！');
      return;
    }
    if (this.data.m_shopAddr.length <= 0) {
      Notify('【学校地址】不能为空，请重新输入！');
      return;
    }
    if (this.data.m_shopPhone.length <= 0) {
      Notify('【学校电话】不能为空，请重新输入！');
      return;
    }
    if (this.data.m_arrArea.length <= 0) {
      Notify('【学校地区】不能为空，请重新选择！');
      return;
    }
    if (parseInt(this.data.m_curMasterID) <= 0) {
      Notify('【学校校长】不能为空，请重新选择！');
      return;
    }
    if (parseInt(this.data.m_curAgentID) <= 0) {
      Notify('【所属机构】不能为空，请重新选择！');
      return;
    }
    // 根据不同的标志进行不同的接口调用...
    this.data.m_bEdit ? this.doShopSave() : this.doShopAdd();
  },
  // 进行学校的添加操作...
  doShopAdd: function() {
    wx.showLoading({ title: '加载中' });
    let that = this;
    // 准备需要的参数信息...
    var thePostData = {
      'name': that.data.m_shopName,
      'addr': that.data.m_shopAddr,
      'phone': that.data.m_shopPhone,
      'agent_id': that.data.m_curAgentID,
      'master_id': that.data.m_curMasterID,
      'province': that.data.m_arrArea[0],
      'city': that.data.m_arrArea[1],
      'area': that.data.m_arrArea[2],
    }
    // 构造访问接口连接地址...
    let theUrl = g_app.globalData.m_urlPrev + 'Mini/addShop';
    // 请求远程API过程...
    wx.request({
      url: theUrl,
      method: 'POST',
      data: thePostData,
      dataType: 'x-www-form-urlencoded',
      header: { 'content-type': 'application/x-www-form-urlencoded' },
      success: function (res) {
        // 隐藏导航栏加载动画...
        wx.hideLoading();
        // 调用接口失败...
        if (res.statusCode != 200) {
          Notify('新建学校记录失败！');
          return;
        }
        // dataType 没有设置json，需要自己转换...
        var arrData = JSON.parse(res.data);
        if (arrData.err_code > 0) {
          Notify(arrData.err_msg);
          return;
        }
        // 再更新一些数据到新建记录当中...
        arrData.shop.user_id = that.data.m_curMasterID;
        arrData.shop.wx_nickname = that.data.m_curMasterName;
        arrData.shop.agent_id = that.data.m_curAgentID;
        arrData.shop.agent_name = that.data.m_curAgentName;
        // 将新得到的学校记录存入父页面当中...
        let pages = getCurrentPages();
        let prevPage = pages[pages.length - 2];
        let theNewShop = new Array();
        theNewShop.push(arrData.shop);
        // 向前插入并更新学校记录到界面当中...
        let theArrShop = prevPage.data.m_arrShop;
        theArrShop = theNewShop.concat(theArrShop);
        prevPage.setData({ m_arrShop: theArrShop, m_total_num: theArrShop.length });
        // 进行页面的更新跳转...
        wx.navigateBack();
      },
      fail: function (res) {
        wx.hideLoading()
        Notify('新建学校记录失败！');
      }
    })
  },
  // 进行学校的保存操作...
  doShopSave: function() {
    // 显示导航栏|浮动加载动画...
    wx.showLoading({ title: '加载中' });
    // 保存this对象...
    let that = this
    let theCurShop = g_app.globalData.m_curSelectItem;
    // 准备需要的参数信息...
    var thePostData = {
      'shop_id': theCurShop.shop_id,
      'name': that.data.m_shopName,
      'addr': that.data.m_shopAddr,
      'phone': that.data.m_shopPhone,
      'agent_id': that.data.m_curAgentID,
      'master_id': that.data.m_curMasterID,
      'province': that.data.m_arrArea[0],
      'city': that.data.m_arrArea[1],
      'area': that.data.m_arrArea[2],
    }
    // 构造访问接口连接地址...
    let theUrl = g_app.globalData.m_urlPrev + 'Mini/saveShop';
    // 请求远程API过程...
    wx.request({
      url: theUrl,
      method: 'POST',
      data: thePostData,
      dataType: 'x-www-form-urlencoded',
      header: { 'content-type': 'application/x-www-form-urlencoded' },
      success: function (res) {
        // 隐藏导航栏加载动画...
        wx.hideLoading();
        // 调用接口失败...
        if (res.statusCode != 200) {
          Notify('更新学校记录失败！');
          return;
        }
        // 进行父页面的数据更新...
        let pages = getCurrentPages();
        let prevPage = pages[pages.length - 2];
        let theIndex = parseInt(theCurShop.indexID);
        let theArrShop = prevPage.data.m_arrShop;
        let thePrevShop = theArrShop[theIndex];
        thePrevShop.name = that.data.m_shopName;
        thePrevShop.addr = that.data.m_shopAddr;
        thePrevShop.phone = that.data.m_shopPhone;
        thePrevShop.user_id = that.data.m_curMasterID;
        thePrevShop.agent_id = that.data.m_curAgentID;
        thePrevShop.master_id = that.data.m_curMasterID;
        thePrevShop.agent_name = that.data.m_curAgentName;
        thePrevShop.wx_nickname = that.data.m_curMasterName;
        thePrevShop.province = that.data.m_arrArea[0];
        thePrevShop.city = that.data.m_arrArea[1];
        thePrevShop.area = that.data.m_arrArea[2];
        prevPage.setData({ m_arrShop: theArrShop });
        // 进行页面的更新跳转...
        wx.navigateBack();
      },
      fail: function (res) {
        wx.hideLoading()
        Notify('更新学校记录失败！');
      }
    })
  },
  // 点击删除按钮...
  doBtnDel: function(event) {
    Dialog.confirm({
      confirmButtonText: "删除",
      title: '学校：' + this.data.m_shopName,
      message: "确实要删除当前选中的学校记录吗？"
    }).then(() => {
      this.doShopDel();
    }).catch(() => {
      console.log("Dialog - Cancel");
    });
  },
  // 进行学校的删除操作...
  doShopDel: function() {
    // 显示导航栏|浮动加载动画...
    wx.showLoading({ title: '加载中' });
    // 保存this对象...
    let that = this
    let theCurShop = g_app.globalData.m_curSelectItem;
    // 准备需要的参数信息...
    var thePostData = {
      'shop_id': theCurShop.shop_id,
      'qrcode': theCurShop.qrcode,
    }
    // 构造访问接口连接地址...
    let theUrl = g_app.globalData.m_urlPrev + 'Mini/delShop';
    // 请求远程API过程...
    wx.request({
      url: theUrl,
      method: 'POST',
      data: thePostData,
      dataType: 'x-www-form-urlencoded',
      header: { 'content-type': 'application/x-www-form-urlencoded' },
      success: function (res) {
        // 隐藏导航栏加载动画...
        wx.hideLoading();
        // 调用接口失败...
        if (res.statusCode != 200) {
          Notify('删除学校记录失败！');
          return;
        }
        // 进行父页面的数据更新...
        let pages = getCurrentPages();
        let prevPage = pages[pages.length - 2];
        let theIndex = parseInt(theCurShop.indexID);
        // 删除并更新学校记录到界面当中...
        prevPage.data.m_arrShop.splice(theIndex,1);
        let theArrShop = prevPage.data.m_arrShop;
        prevPage.setData({ m_arrShop: theArrShop, m_total_num: theArrShop.length });
        // 进行页面的更新跳转...
        wx.navigateBack();
      },
      fail: function (res) {
        wx.hideLoading()
        Notify('删除学校记录失败！');
      }
    })
  },
  // 学校名称发生变化...
  onNameChange: function(event) {
    this.data.m_shopName = event.detail;
  },
  // 学校地址发生变化...
  onAddrChange: function(event) {
    this.data.m_shopAddr = event.detail;
  },
  // 学校电话发生变化...
  onPhoneChange: function (event) {
    this.data.m_shopPhone = event.detail;
  },
  // 地区发生选择变化...
  onAreaChange: function(event) {
    const { code: currentCode, value: currentValue } = event.detail;
    this.setData({
      m_arrArea: currentValue,
      m_curArea: currentValue.join(' / '),
    })
  },
  // 校长发生选择变化...
  onMasterChange: function(event) {
    const { value } = event.detail;
    let theNewMasterID = parseInt(value);
    if (theNewMasterID < 0 || theNewMasterID >= this.data.m_arrMaster.length) {
      Notify('【学校校长】选择内容越界！');
      return;
    }
    // 获取到当前变化后的校长信息，并写入数据然后显示出来...
    let theCurMaster = this.data.m_arrMaster[theNewMasterID];
    this.setData({ m_curMasterID: theCurMaster.user_id, m_curMasterName: theCurMaster.wx_nickname });
  },
  // 所属机构发生选择变化...
  onAgentChange: function (event) {
    const { value } = event.detail;
    let theNewAgentID = parseInt(value);
    if (theNewAgentID < 0 || theNewAgentID >= this.data.m_arrAgent.length) {
      Notify('【所属机构】选择内容越界！');
      return;
    }
    // 获取到当前变化后的校长信息，并写入数据然后显示出来...
    let theCurAgent = this.data.m_arrAgent[theNewAgentID];
    this.setData({ m_curAgentID: theCurAgent.agent_id, m_curAgentName: theCurAgent.name });
  },
  /**
   * 生命周期函数--监听页面初次渲染完成
   */
  onReady: function () {

  },

  /**
   * 生命周期函数--监听页面显示
   */
  onShow: function () {

  },

  /**
   * 生命周期函数--监听页面隐藏
   */
  onHide: function () {

  },

  /**
   * 生命周期函数--监听页面卸载
   */
  onUnload: function () {

  },

  /**
   * 页面相关事件处理函数--监听用户下拉动作
   */
  onPullDownRefresh: function () {

  },

  /**
   * 页面上拉触底事件的处理函数
   */
  onReachBottom: function () {

  },

  /**
   * 用户点击右上角分享
   */
  onShareAppMessage: function () {

  }
})