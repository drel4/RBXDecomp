#include "../scene.h"
#include "../schema.h"

#include <cstdio>
#include <stdexcept>

static void require(bool condition, const char* message)
{
	if (!condition) {
		throw std::runtime_error(message);
	}
}
int main(int argc, char** argv)
{
	try {
		{
			openrbx::Instance object;
			object.className = "Part";
			object.properties.push_back({"Anchored", "bool", "false"});
			object.properties.push_back({"Friction", "float", "0.300000012"});
			object.properties.push_back(openrbx::vectorProperty("Velocity", {}));
			object.properties.push_back(openrbx::frameProperty("CFrame", {}));
			object.properties.push_back({"UnknownData", "string", "preserve exactly"});
			openrbx::compactDefaults(object);
			require(object.properties.size() == 1, "Inherited default storage was not compacted");
			require(
				object.property("Friction")->text == "0.3" && object.property("Anchored")->text == "false" &&
					object.property("UnknownData")->text == "preserve exactly",
				"Compaction changed defaults or unknown data"
			);
			object.properties.push_back({"Transparency", "float", "0"});
			object.properties.push_back({"Transparency", "float", "0.5"});
			object.properties.push_back({"CanCollide", "bool", "false"});
			object.properties.push_back({"Velocity", "Vector3", "", {{"Unexpected", "0"}}});
			openrbx::compactDefaults(object);
			require(
				object.properties.size() == 5 && object.property("Transparency")->text == "0" &&
					object.property("CanCollide")->text == "false",
				"Overrides, duplicates or unknown components lost"
			);
			openrbx::Instance joint;
			joint.className = "Weld";
			joint.properties.push_back({"Part0", "Ref", "RBX1"});
			joint.properties.push_back({"Part1", "Ref", "null"});
			openrbx::compactDefaults(joint);
			require(
				joint.properties.size() == 1 && joint.property("Part0")->text == "RBX1" &&
					joint.property("Part1")->reference == openrbx::NoInstance,
				"Unresolved Ref was elided"
			);
		}
		{
			openrbx::Scene shared;
			std::string failure;
			require(openrbx::loadPlace("tests/SharedProperties.rbxl", shared, failure), failure.c_str());
			auto& model = shared.model;
			auto workspace = model.service("Workspace");
			auto children = model.get(workspace)->children;
			require(children.size() == 6, "Shared-property fixture lost objects");
			require(
				model.get(children[0])->properties.identity() == model.get(children[1])->properties.identity() &&
					model.get(children[2])->properties.identity() == model.get(children[3])->properties.identity(),
				"Loaded repeated property/source arrays did not share storage"
			);
			require(
				model.get(children[4])->property("Part0")->reference == children[0] &&
					model.get(children[5])->property("Part0")->reference == children[1],
				"Pooled references aliased targets"
			);
			model.setProperty(children[0], {"Anchored", "bool", "true"});
			require(
				model.get(children[1])->property("Anchored")->text == "false" &&
					model.get(children[0])->properties.identity() != model.get(children[1])->properties.identity(),
				"Loaded COW mutation changed another part"
			);
			require(
				model.get(children[1])->property("TopParamA")->text == "-0.5" && model.pspBytes() <= model.bytes(),
				"Surface defaults or PSP-layout memory accounting incorrect"
			);
		}
		openrbx::Scene scene;
		std::string error;
		require(argc == 2, "fixture argument required");
		require(openrbx::loadPlace(argv[1], scene, error), error.c_str());
		auto& model = scene.model;
		auto root = model.root();
		auto workspace = model.findChild(root, "Workspace");
		auto group = model.findChild(workspace, "Group");
		auto platform = model.findChild(group, "A & B");
		auto hidden = model.findChild(group, "Hidden");
		auto script = model.findChild(platform, "Script");
		auto weld = model.findChild(group, "Weld");
		require(root && workspace && group && platform && hidden && script && weld, "hierarchy/name lost");
		require(model.get(hidden)->renderPart == -1, "invisible part should not draw");
		require(model.get(hidden)->property("CanCollide")->text == "true", "invisible collision property lost");
		require(model.get(platform)->renderPart == 0 && scene.parts.size() == 1, "geometry not linked to instance");
		require(
			model.get(script)->property("Source")->text == "if 1 < 2 then\nprint('a &amp; b')\nend -- & literal",
			"CDATA/entity source corrupted"
		);
		require(model.get(platform)->property("size")->components.size() == 3, "structured size lost");
		require(model.get(weld)->property("Part0")->reference == platform, "forward reference not bound");
		require(model.get(weld)->property("Part1")->reference == hidden, "backward reference not bound");
		require(model.findChild(workspace, "Script", true) == script, "recursive lookup failed");
		require(model.service("Workspace") == workspace, "duplicate service");
		require(model.isDescendant(script, workspace) && !model.isDescendant(workspace, script), "ancestry incorrect");
		bool rejected = false;
		try {
			model.setParent(group, script);
		}
		catch (const std::exception&) {
			rejected = true;
		}
		require(rejected && model.get(group)->parent == workspace, "cyclic parenting changed tree");
		auto copy = model.clone(group);
		auto copyPlatform = model.findChild(copy, "A & B");
		auto copyWeld = model.findChild(copy, "Weld");
		require(copy && model.get(copy)->parent == 0 && copyPlatform != platform, "clone failed");
		require(model.get(copyWeld)->property("Part0")->reference == copyPlatform, "clone kept original internal Ref");
		require(model.get(copyPlatform)->renderPart == -1, "clone aliases original render slot");
		model.setProperty(copyPlatform, openrbx::vectorProperty("size", openrbx::Vec3(9, 3, 2)));
		require(
			openrbx::propertyVector(*model.get(platform)->property("size")).x == 4,
			"Clone property mutation changed original snapshot"
		);
		model.setProperty(platform, openrbx::frameProperty("CFrame", openrbx::CFrame(openrbx::Vec3(3, 7, 11))));
		require(
			openrbx::propertyFrame(*model.get(copyPlatform)->property("CFrame")).position == openrbx::Vec3(),
			"Original property mutation changed clone snapshot"
		);
		model.setParent(copy, workspace);
		model.remove(group);
		require(model.get(group)->parent == 0 && model.get(group)->children.empty(), "Remove didn't detach subtree");
		require(model.get(script)->parent == 0 && model.get(platform), "Remove invalidated held handle");
		require(model.get(copy)->parent == workspace, "Remove affected clone");
		openrbx::DataModel imported;
		auto destination = imported.service("Workspace");
		auto importedCopy = imported.import(model, copy, destination);
		auto importedPlatform = imported.findChild(importedCopy, "A & B");
		auto importedWeld = imported.findChild(importedCopy, "Weld");
		require(
			imported.get(importedWeld)->property("Part0")->reference == importedPlatform,
			"Asset import did not remap Ref"
		);
		scene.clear();
		require(
			imported.get(importedPlatform)->className == "Part" &&
				imported.get(importedPlatform)->property("size")->components[0].first == "X",
			"Asset import retained dangling source symbols"
		);
		require(!scene.model.root() && scene.bytes() == 0, "reload did not release runtime storage");
		for (const char* asset :
			 {"content/character.rbxm", "content/humanoidAnimate.rbxm", "content/humanoidHealth.rbxm"}) {
			openrbx::Scene assetScene;
			require(openrbx::loadPlace(asset, assetScene, error, true), error.c_str());
			auto children = assetScene.model.get(assetScene.model.root())->children;
			require(children.size() == 1, "Character asset root count changed");
			auto importedAsset = imported.import(assetScene.model, children[0]);
			assetScene.clear();
			const auto* object = imported.get(importedAsset);
			if (object->className == "Model") {
				require(
					imported.findChild(importedAsset, "Humanoid") && imported.findChild(importedAsset, "Torso"),
					"Real character asset parts lost"
				);
			}
			else {
				require(
					object->className == "Script" && !object->property("Source")->text.empty(),
					"Real character script asset lost"
				);
			}
		}
		std::puts(
			"PASS: loaded hierarchy, source, values, Ref linking, service identity, clone remapping, Remove and "
			"teardown"
		);
		return 0;
	}
	catch (const std::exception& e) {
		std::fprintf(stderr, "FAIL: %s\n", e.what());
		return 1;
	}
}
