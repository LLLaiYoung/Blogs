# Clang Plugin 之 Debug

前面一篇文章 [LLVM & Clang 入门](https://github.com/CYBoys/Blogs/blob/master/LLVM_Clang/LLVM%20%26%20Clang%20%E5%85%A5%E9%97%A8.md) 讲了如何编写一个 Clang 插件，然后将插件编译成一个`.dylib`的动态链接库。集成到 Xcode 中就可以看到效果（正确的结果）。

在得到正确结果的过程中，必不可少的一步就是`Debug`，没有任何程序是一蹴而就的，除非你`printf`一个`"Hello, World!"`，说不定你的`world`还写成了`word`。

在使用`Plugin`的模式下我们是不能打断点进行 Debug 的，但是我们可以在代码中加日志，然后在终端中执行命令看日志进行 Debug。这种低效率（Low）的方式是你想要的吗？显然不是。

我们只需要把`.dylib`动态库变成`可执行文件`就能打断点 debug。[LibTooling](https://clang.llvm.org/docs/LibTooling.html) 或许是一个不错的选择。使用 LibTooling 的话，我们只需要改动很少部分的代码就可以。

**LibTooling 简介**：

LibTooling 是一个独立的库，它允许使用者很方便地搭建属于你自己的编译器前端工具，它基于 C++ 接口，提供给使用者强大全面的 AST 解析和控制能力，同时由于它与 Clang 的内核过于接近导致它的版本兼容能力比 libclang 差得多，Clang 的变动很容易影响到 LibTooling。libTooling 还提供了完整的参数解析方案，可以很方便的构建一个独立的命令行工具。

### 创建 LibTooling 项目及代码调整

为了方便，我们直接创建一个可执行的`LibTooling`项目，我们可以创建一个名为`QTPluginTooling`的项目。

1. 创建过程跟 [创建插件](https://github.com/CYBoys/Blogs/blob/master/LLVM_Clang/LLVM%20%26%20Clang%20%E5%85%A5%E9%97%A8.md#create_plugin) 步骤差不多，前面 3 步都是一样的，只需要把`QTPlugin`替换为`QTPluginTooling`就可以。

2. 只是在第 4 步略有不同，`QTPluginTooling`目录下的`CMakeLists.txt`的文件内容为

    ```
    set(LLVM_LINK_COMPONENTS
        Support
    )
    
    add_clang_executable(QTPluginTooling
        QTPluginTooling.cpp
    )
    
    target_link_libraries(QTPluginTooling
        PRIVATE
        clangAST
        clangBasic
        clangDriver
        clangFormat
        clangLex
        clangParse
        clangSema
        clangFrontend
        clangTooling
        clangToolingCore
        clangRewrite
        clangRewriteFrontend
    )
    
    if (UNIX)
        set(CLANGXX__LING_OR_COPY create_symlink)
    else()
        set(CLANGXX_LINK_OR_COPY copy)
    endif()
    ```
    
    ![tooling_cmakeLists](https://raw.githubusercontent.com/CYBoys/Blogs/master/Pic/clang_tooling_cmakelists.png)

3. 在`llvm_xcode`目录下执行`$ cmake -G Xcode ../llvm`，重新生成一下`Xcode`项目。`Tooling`项目在 Xcode 的`Clang executables`目录下可以找到。

    ![](https://raw.githubusercontent.com/CYBoys/Blogs/master/Pic/clang_executables_tooling.png)

4. 将之前 [Plugin](https://github.com/CYBoys/Blogs/blob/master/LLVM_Clang/Examples/QTPlugin/QTPlugin.cpp) 的代码复制过来，新增三个头文件

    ```
    #include "clang/Tooling/CommonOptionsParser.h"
    #include "clang/Frontend/FrontendActions.h"
    #include "clang/Tooling/Tooling.h"
    ```

5. 新增一个命名空间

    ```
    using namespace clang::tooling;
    ```

6. 将`QTASTAction`的继承改为继承至`ASTFrontendAction`。

7. 将`FrontendPluginRegistry`注册插件的方式注释。更改为`main()`函数方式

    ```
    static llvm::cl::OptionCategory OptsCategory("QTPlugin");
    int main(int argc, const char **argv) {
        CommonOptionsParser op(argc, argv, OptsCategory);
        ClangTool Tool(op.getCompilations(), op.getSourcePathList());
        return Tool.run(newFrontendActionFactory<QTPlugin::QTASTAction>().get());
    }
    ```

**最后整个文件的内容如下**：

```
#include <iostream>
#include "clang/AST/AST.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;
using namespace std;
using namespace llvm;
using namespace clang::ast_matchers;
using namespace clang::tooling;

namespace QTPlugin {
    
    class QTMatchHandler: public MatchFinder::MatchCallback {
    private:
        CompilerInstance &CI;
        
        bool isUserSourceCode(const string filename) {
            if (filename.empty()) return false;
            
            // 非Xcode中的源码都认为是用户源码
            if (filename.find("/Applications/Xcode.app/") == 0) return false;
            
            return true;
        }
        
        bool isShouldUseCopy(const string typeStr) {
            if (typeStr.find("NSString") != string::npos ||
                typeStr.find("NSArray") != string::npos ||
                typeStr.find("NSDictionary") != string::npos/*...*/) {
                return true;
            }
            return false;
        }
    public:
        QTMatchHandler(CompilerInstance &CI) :CI(CI) {}
        
        void run(const MatchFinder::MatchResult &Result) {
            const ObjCPropertyDecl *propertyDecl = Result.Nodes.getNodeAs<ObjCPropertyDecl>("objcPropertyDecl");
            if (propertyDecl && isUserSourceCode(CI.getSourceManager().getFilename(propertyDecl->getSourceRange().getBegin()).str()) ) {
                ObjCPropertyDecl::PropertyAttributeKind attrKind = propertyDecl->getPropertyAttributes();
                string typeStr = propertyDecl->getType().getAsString();
                
                if (propertyDecl->getTypeSourceInfo() && isShouldUseCopy(typeStr) && !(attrKind & ObjCPropertyDecl::OBJC_PR_copy)) {
                    cout<<"--------- "<<typeStr<<": 不是使用的 copy 修饰--------"<<endl;
                    DiagnosticsEngine &diag = CI.getDiagnostics();
                    diag.Report(propertyDecl->getBeginLoc(), diag.getCustomDiagID(DiagnosticsEngine::Warning, "--------- %0 不是使用的 copy 修饰--------")) << typeStr;
                }
            }
        }
    };
    
    class QTASTConsumer: public ASTConsumer {
    private:
        MatchFinder matcher;
        QTMatchHandler handler;
    public:
        QTASTConsumer(CompilerInstance &CI) :handler(CI) {
            matcher.addMatcher(objcPropertyDecl().bind("objcPropertyDecl"), &handler);
        }
        
        void HandleTranslationUnit(ASTContext &context) {
            matcher.matchAST(context);
        }
    };
    
    class QTASTAction: public ASTFrontendAction {
    public:
        unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef iFile) {
            return unique_ptr<QTASTConsumer> (new QTASTConsumer(CI));
        }
        
        bool ParseArgs(const CompilerInstance &ci, const std::vector<std::string> &args) {
            return true;
        }
    };
}

//static FrontendPluginRegistry::Add<QTPlugin::QTASTAction> X("QTPlugin", "The QTPlugin is my first clang-plugin.");

static llvm::cl::OptionCategory OptsCategory("QTPlugin");
int main(int argc, const char **argv) {
    CommonOptionsParser op(argc, argv, OptsCategory);
    ClangTool Tool(op.getCompilations(), op.getSourcePathList());
    return Tool.run(newFrontendActionFactory<QTPlugin::QTASTAction>().get());
}
```

### 输入源

如果这时候就`run`的话则会直接退出。这是因为没有“输入源”。我们可以在`QTPluginTooling`的`Scheme`加入。

```
/Users/laiyoung_/Desktop/Plugin/ViewController.m
--
-isysroot
/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator.sdk
-isystem
-I/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/clang/10.0.0/include
-I/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/include/c++/v1
-I/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator.sdk/usr/include
-F/Applications/Xcode.app/Contents/Developer/Platforms/iPhoneSimulator.platform/Developer/SDKs/iPhoneSimulator.sdk/System/Library/Frameworks
```

![](https://raw.githubusercontent.com/CYBoys/Blogs/master/Pic/clang_tooling_xcode_schene_arguments.png)

上面`--`是告诉你要编译这个`ViewController.m`文件必须使用下面`-I`，`-F`标出来的库，也就是当编译器执行到`ViewController.m`里面的`#import`这段代码的时候，系统会在你标注中的`-I`，`-F`的库寻找相应的文件，编译才能正常执行下去。否则会报找不到的错误。

**参数解释**：

> 上面在`--`后面的参数，是传递给`CI`的`Compilation DataBase`的，而不是这个命令行工具本身的。比如我们的`ViewController.m`，因为有`#import <UIKit/UIKit.h>`这么一条语句，以及继承了`UIViewController`，那么语法分析器(Sema)读到这里的时候就需要知道`UIViewController`的定义是从哪里来的，换句话说就是它需要找到定义`UIViewController`的地方。怎么找呢？通过指定的`-I`、`-F`这些参数指定的目录来寻找。`--`后面的参数，可以理解为如果你要编译`ViewController.m`需要什么参数，那么这个后面就要传递什么参数给我们的 `QTPlugin`，否则就会看到`Console`里打出找不到`xxx`定义或者`xxx.h`文件的错误。当然因为一般的编译指令，会有`-c`参数指定源文件，但是`--`后面并不需要，因为我们在`--`前面就指定了。`--`这种传参的方式还有另外一种方法，使用`-extra-arg="xxxx"`的方式指定编译参数，这样就不需要`--`了。

> ```
-extra-arg="-Ixxxxxx"
-extra-arg="-Fxxxxxx"
-extra-arg="-isysroot xxxxxx"
xxxxxx表示的路径
```

#### 最终效果：

![](https://raw.githubusercontent.com/CYBoys/Blogs/master/Pic/clang_tooling_debuging.png)

**参考文章**：

* [打造基于Clang LibTooling的iOS自动打点系统CLAS（二）](https://www.jianshu.com/p/01c988cae897)
* [Compilation databases for Clang-based tools](https://eli.thegreenplace.net/2014/05/21/compilation-databases-for-clang-based-tools)

如有内容错误，欢迎 [issue](https://github.com/CYBoys/Blogs/issues/new) 指正。

**[Example](https://github.com/CYBoys/Blogs/tree/master/LLVM_Clang/Examples/QTPluginTooling)**

**转载请注明出处！**

