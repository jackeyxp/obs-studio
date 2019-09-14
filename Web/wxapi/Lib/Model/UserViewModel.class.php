<?php

class UserViewModel extends ViewModel {
    public $viewFields = array(
        'User'=>array('*','_type'=>'LEFT'), //Must be LEFT 返回左表所有数据，即使右表没有匹配的
        'Child'=>array('*','_on'=>'User.child_id=Child.child_id','_type'=>'LEFT'),
        'Shop'=>array('shop_id','name'=>'shop_name','_on'=>'User.shop_id=Shop.shop_id')
    );
}
?>