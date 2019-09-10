/** 
 * jquery arrow
 * @author:xuzengqiang 
 * @since :2015-2-4 15:31:39 
**/  
;(function($){     
  jQuery.fn.extend({  
    arrow:function(options){  
        var defaultOptions = {  
                color:'#AFAFAF',  
                height:20,  
                width:20,  
                direction:'top' //['up','bottom','left','right']
            };  
        var settings = jQuery.extend(defaultOptions,options||{}),  
            current = $(this);  
        function loadStyle(){  
            current.css({"display":"block","width":"0","height":"0"});  
            if(settings.direction === "top" || settings.direction === "bottom") {  
                current.css({  
                            "border-left-width":settings.width/2,  
                            "border-right-width":settings.width/2,  
                            "border-left-style":"solid",  
                            "border-right-style":"solid",  
                            "border-left-color":"transparent",  
                            "border-right-color":"transparent"  
                            });  
                if(settings.direction === "top") {  
                    current.css({  
                            "border-bottom-width":settings.height,  
                            "border-bottom-style":"solid",  
                            "border-bottom-color":settings.color      
                            });  
                } else {  
                    current.css({  
                            "border-top-width":settings.height,  
                            "border-top-style":"solid",  
                            "border-top-color":settings.color     
                            });  
                }  
            } else if(settings.direction === "left" || settings.direction === "right") {  
                current.css({  
                            "border-top-width":settings.height/2,  
                            "border-bottom-width":settings.height/2,  
                            "border-top-style":"solid",  
                            "border-bottom-style":"solid",  
                            "border-top-color":"transparent",  
                            "border-bottom-color":"transparent"  
                            });  
                if(settings.direction === "left") {  
                    current.css({  
                            "border-right-width":settings.width,  
                            "border-right-style":"solid",  
                            "border-right-color":settings.color   
                            });  
                } else {  
                    current.css({  
                            "border-left-width":settings.width,  
                            "border-left-style":"solid",  
                            "border-left-color":settings.color    
                            });  
                }  
            }  
        }  
        return this.each(function(){ loadStyle(); });  
    }  
  });  
})(jQuery);