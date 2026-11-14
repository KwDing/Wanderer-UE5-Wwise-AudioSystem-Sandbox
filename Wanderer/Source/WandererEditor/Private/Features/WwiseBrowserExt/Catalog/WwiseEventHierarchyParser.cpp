#include "Features/WwiseBrowserExt/Catalog/WwiseEventHierarchyParser.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "XmlFile.h"

#include "Features/WwiseBrowserExt/Catalog/BrowserCatalogTypes.h"

namespace WwiseEventHierarchy
{

    bool Collect(const FXmlNode& InXml,
        const FGuid& InParent,
        const FString& InRoot,
        TMap<FGuid, FWwiseEventHierarchyNode>& InOutNodes,
        FString& OutError)
    {
        const FString Tag = InXml.GetTag();
        if (Tag != TEXT("WorkUnit") && Tag != TEXT("Folder") && Tag != TEXT("Event"))
        {
            return true;
        }
        FGuid GUID;
        const FString Name = InXml.GetAttribute(TEXT("Name"));
        if (!FGuid::Parse(InXml.GetAttribute(TEXT("ID")), GUID) || !GUID.IsValid() || Name.IsEmpty()
            || Name.Contains(TEXT("/")) || Name.Contains(TEXT("\\")) || Name == TEXT(".")
            || Name == TEXT(".."))
        {
            OutError = TEXT("Wwise Event 工程包含无效对象 ID 或名称。");
            return false;
        }
        FGuid Owner = InParent;
        if (!Owner.IsValid())
        {
            FGuid::Parse(InXml.GetAttribute(TEXT("OwnerID")), Owner);
        }
        if (const FWwiseEventHierarchyNode* Existing = InOutNodes.Find(GUID))
        {
            if (Existing->Name != Name
                || (Existing->Parent.IsValid() && Owner.IsValid() && Existing->Parent != Owner))
            {
                OutError = TEXT("Wwise Event 工程包含冲突的对象层级。");
                return false;
            }
            if (!Owner.IsValid())
            {
                Owner = Existing->Parent;
            }
        }
        InOutNodes.Add(GUID, {Name, Owner, InRoot, Tag == TEXT("Event")});
        if (Tag == TEXT("Event"))
        {
            return true; // Actions and ObjectRef targets are not hierarchy objects.
        }
        if (const FXmlNode* Children = InXml.FindChildNode(TEXT("ChildrenList")))
        {
            for (const FXmlNode* Child : Children->GetChildrenNodes())
            {
                if (!Collect(*Child, GUID, InRoot, InOutNodes, OutError))
                {
                    return false;
                }
            }
        }
        return true;
    }

    bool Resolve(const FGuid& InGUID,
        const TMap<FGuid, FWwiseEventHierarchyNode>& InNodes,
        TSet<FGuid>& InOutVisiting,
        FString& OutPath)
    {
        const FWwiseEventHierarchyNode* Node = InNodes.Find(InGUID);
        if (!Node || InOutVisiting.Contains(InGUID))
        {
            return false;
        }
        InOutVisiting.Add(InGUID);
        FString ParentPath = Node->Root;
        if (Node->Parent.IsValid() && !Resolve(Node->Parent, InNodes, InOutVisiting, ParentPath))
        {
            return false;
        }
        OutPath = ParentPath / Node->Name;
        InOutVisiting.Remove(InGUID);
        return true;
    }
} // namespace WwiseEventHierarchy

bool FWwiseEventHierarchyParser::Parse(
    const FString& InEventsDirectory, TMap<FGuid, FString>& OutPaths, FString& OutError)
{
    OutPaths.Reset();
    TArray<FString> Files;
    IFileManager::Get().FindFilesRecursive(Files, *InEventsDirectory, TEXT("*.wwu"), true, false);
    if (Files.IsEmpty())
    {
        OutError = TEXT("无法读取已保存的 Wwise Events 工程，取消同步/删除。请先保存 Wwise。");
        return false;
    }
    TMap<FGuid, FWwiseEventHierarchyNode> Nodes;
    for (const FString& File : Files)
    {
        const FXmlFile Xml(File);
        const FXmlNode* Document = Xml.GetRootNode();
        const FXmlNode* Events = Document ? Document->FindChildNode(TEXT("Events")) : nullptr;
        if (!Xml.IsValid() || !Events)
        {
            OutError = TEXT("无法解析 Wwise Events 文件：") + File;
            return false;
        }
        FString Relative = File;
        FString Base = InEventsDirectory;
        FPaths::NormalizeDirectoryName(Base);
        Base += TEXT("/");
        if (!FPaths::MakePathRelativeTo(Relative, *Base))
        {
            OutError = TEXT("无法解析 Events 相对目录。");
            return false;
        }
        const FString Root = TEXT("/Events") / FPaths::GetPath(Relative);
        for (const FXmlNode* Unit : Events->GetChildrenNodes())
        {
            if (!WwiseEventHierarchy::Collect(*Unit, FGuid(), Root, Nodes, OutError))
            {
                return false;
            }
        }
    }
    for (const TPair<FGuid, FWwiseEventHierarchyNode>& Entry : Nodes)
    {
        if (!Entry.Value.bEvent)
        {
            continue;
        }
        TSet<FGuid> Visiting;
        FString Path;
        if (!WwiseEventHierarchy::Resolve(Entry.Key, Nodes, Visiting, Path))
        {
            OutError = TEXT("Wwise Event 层级缺失父对象或包含循环，取消同步。");
            OutPaths.Reset();
            return false;
        }
        OutPaths.Add(Entry.Key, MoveTemp(Path));
    }
    return true; // An empty, valid Events hierarchy is authoritative too.
}
