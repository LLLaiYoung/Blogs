#include <iostream>
#include "clang/AST/AST.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/FrontendPluginRegistry.h"

using namespace clang;
using namespace std;
using namespace llvm;
using namespace clang::ast_matchers;

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
    
    class QTASTAction: public PluginASTAction {
    public:
        unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef iFile) {
            return unique_ptr<QTASTConsumer> (new QTASTConsumer(CI));
        }
        
        bool ParseArgs(const CompilerInstance &ci, const std::vector<std::string> &args) {
            return true;
        }
    };
}

static FrontendPluginRegistry::Add<QTPlugin::QTASTAction> X("QTPlugin", "The QTPlugin is my first clang-plugin.");
