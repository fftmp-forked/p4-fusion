/*
 * Copyright (c) 2022 Salesforce, Inc.
 * All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * For full license text, see the LICENSE.txt file in the repo root or https://opensource.org/licenses/BSD-3-Clause
 */
#include "git_api.h"

#include <cstring>
#include <cstdlib>

#include "git2.h"
#include "git2/sys/repository.h"
#include "minitrace.h"
#include "utils/std_helpers.h"

#define GIT2(x)                                                                \
	do                                                                         \
	{                                                                          \
		int error = x;                                                         \
		if (error < 0)                                                         \
		{                                                                      \
			const git_error* e = git_error_last();                             \
			ERR("GitAPI: " << error << ":" << e->klass << ": " << e->message); \
			exit(error);                                                       \
		}                                                                      \
	} while (false)

GitAPI* GitAPI::singleton = nullptr;

void GitAPI::MakeSingleton(bool fsyncEnable)
{
	static GitAPI object(fsyncEnable);
	singleton = &object;
}

GitAPI* GitAPI::GetSingleton()
{
	return singleton;
}

GitAPI::GitAPI(bool fsyncEnable)
{
	git_libgit2_init();
	GIT2(git_libgit2_opts(GIT_OPT_ENABLE_FSYNC_GITDIR, (int)fsyncEnable));
}

GitAPI::~GitAPI()
{
	if (m_Repo)
	{
		git_repository_free(m_Repo);
		m_Repo = nullptr;
	}
	git_libgit2_shutdown();
}

bool GitAPI::IsRepositoryClonedFrom(const std::string& depotPath)
{
	git_oid oid;
	GIT2(git_reference_name_to_id(&oid, m_Repo, "HEAD"));

	git_commit* headCommit = nullptr;
	GIT2(git_commit_lookup(&headCommit, m_Repo, &oid));

	std::string message = git_commit_message(headCommit);
	size_t depotPathStart = message.find("depot-paths = \"") + 15;
	size_t depotPathEnd = message.find("\": change") - 1;
	std::string repoDepotPath = message.substr(depotPathStart, depotPathEnd - depotPathStart + 1) + "...";

	git_commit_free(headCommit);

	return repoDepotPath == depotPath;
}

void GitAPI::OpenRepository(const std::string& repoPath)
{
	GIT2(git_repository_open(&m_Repo, repoPath.c_str()));
}

bool GitAPI::InitializeRepository(const std::string& srcPath)
{
	GIT2(git_repository_init(&m_Repo, srcPath.c_str(), true));
	SUCCESS("Initialized Git repository at " << srcPath);

	return true;
}

bool GitAPI::IsHEADExists()
{
	git_oid oid;
	int errorCode = git_reference_name_to_id(&oid, m_Repo, "HEAD");
	if (errorCode && errorCode != GIT_ENOTFOUND)
	{
		GIT2(errorCode);
	}
	return errorCode == 0;
}

git_oid GitAPI::CreateBlob(const std::vector<char>& data)
{
	git_oid oid;
	GIT2(git_blob_create_from_buffer(&oid, m_Repo, data.data(), data.size()));
	return oid;
}

std::string GitAPI::DetectLatestCL()
{
	git_oid oid;
	GIT2(git_reference_name_to_id(&oid, m_Repo, "HEAD"));

	git_commit* headCommit = nullptr;
	GIT2(git_commit_lookup(&headCommit, m_Repo, &oid));

	std::string message = git_commit_message(headCommit);
	size_t clStart = message.find_last_of("change = ") + 1;
	std::string cl(message.begin() + clStart, message.end() - 1);

	git_commit_free(headCommit);

	return cl;
}

void GitAPI::CreateIndex()
{
	MTR_SCOPE("Git", __func__);

	GIT2(git_repository_index(&m_Index, m_Repo));

	if (IsHEADExists())
	{
		git_oid oid_parent_commit = {};
		GIT2(git_reference_name_to_id(&oid_parent_commit, m_Repo, "HEAD"));

		git_commit* head_commit = nullptr;
		GIT2(git_commit_lookup(&head_commit, m_Repo, &oid_parent_commit));

		git_tree* head_commit_tree = nullptr;
		GIT2(git_commit_tree(&head_commit_tree, head_commit));

		GIT2(git_index_read_tree(m_Index, head_commit_tree));

		git_tree_free(head_commit_tree);
		git_commit_free(head_commit);

		WARN("Loaded index was refreshed to match the tree of the current HEAD commit");
	}
	else
	{
		WARN("No HEAD commit was found. Created a fresh index.");
	}
}

void GitAPI::AddFileToIndex(const std::string& depotPath, const std::string& depotFile, const git_oid& oid, const bool plusx)
{
	MTR_SCOPE("Git", __func__);

	git_index_entry entry = {};
	entry.mode = GIT_FILEMODE_BLOB;
	entry.id = oid;
	if (plusx)
	{
		entry.mode = GIT_FILEMODE_BLOB_EXECUTABLE; // 0100755
	}

	std::string depotPathTrunc = depotPath.substr(0, depotPath.size() - 3); // -3 to remove trailing ...
	std::string gitFilePath = depotFile;
	STDHelpers::Erase(gitFilePath, depotPathTrunc);

	entry.path = gitFilePath.c_str();
	GIT2(git_index_add(m_Index, &entry));
}

void GitAPI::RemoveFileFromIndex(const std::string& depotPath, const std::string& depotFile)
{
	MTR_SCOPE("Git", __func__);

	std::string depotPathTrunc = depotPath.substr(0, depotPath.size() - 3); // -3 to remove trailing ...
	std::string gitFilePath = depotFile;
	STDHelpers::Erase(gitFilePath, depotPathTrunc);

	GIT2(git_index_remove_bypath(m_Index, gitFilePath.c_str()));
}

std::string GitAPI::Commit(
    const std::string& depotPath,
    const std::string& cl,
    const std::string& user,
    const std::string& email,
    const int& timezone,
    const std::string& desc,
    const int64_t& timestamp)
{
	MTR_SCOPE("Git", __func__);

	git_oid commitTreeID;
	GIT2(git_index_write_tree_to(&commitTreeID, m_Index, m_Repo));

	git_tree* commitTree = nullptr;
	GIT2(git_tree_lookup(&commitTree, m_Repo, &commitTreeID));

	git_signature* author = nullptr;
	GIT2(git_signature_new(&author, user.c_str(), email.c_str(), timestamp, timezone));

	git_reference* ref = nullptr;
	git_object* parent = nullptr;
	{
		int error = git_revparse_ext(&parent, &ref, m_Repo, "HEAD");
		if (error == GIT_ENOTFOUND)
		{
			WARN("GitAPI: HEAD not found. Creating first commit");
		}
	}

	git_oid commitID;
	// -3 to remove the trailing "..."
	std::string commitMsg = cl + " - " + desc + "\n[p4-fusion: depot-paths = \"" + depotPath.substr(0, depotPath.size() - 3) + "\": change = " + cl + "]";
	GIT2(git_commit_create_v(&commitID, m_Repo, "HEAD", author, author, "UTF-8", commitMsg.c_str(), commitTree, parent ? 1 : 0, parent));

	git_object_free(parent);
	git_reference_free(ref);
	git_signature_free(author);
	git_tree_free(commitTree);

	return git_oid_tostr_s(&commitID);
}

void GitAPI::CloseIndex()
{
	GIT2(git_index_write(m_Index));
	git_index_free(m_Index);
}
