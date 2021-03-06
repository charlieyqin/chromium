// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// A complete set of unit tests for GaiaAuthFetcher.
// Originally ported from GoogleAuthenticator tests.

#include <string>

#include "base/json/json_reader.h"
#include "base/message_loop.h"
#include "base/stringprintf.h"
#include "base/values.h"
#include "chrome/common/net/gaia/gaia_auth_consumer.h"
#include "chrome/common/net/gaia/gaia_auth_fetcher.h"
#include "chrome/common/net/gaia/gaia_urls.h"
#include "chrome/common/net/gaia/google_service_auth_error.h"
#include "chrome/common/net/gaia/mock_url_fetcher_factory.h"
#include "chrome/test/base/testing_profile.h"
#include "content/public/common/url_fetcher_delegate.h"
#include "content/test/test_url_fetcher_factory.h"
#include "googleurl/src/gurl.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/http/http_status_code.h"
#include "net/url_request/url_request_status.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::_;
using ::testing::Invoke;

namespace {
static const char kGetAuthCodeValidCookie[] =
    "oauth_code=test-code; Path=/test; Secure; HttpOnly";
static const char kGetAuthCodeCookieNoSecure[] =
    "oauth_code=test-code; Path=/test; HttpOnly";
static const char kGetAuthCodeCookieNoHttpOnly[] =
    "oauth_code=test-code; Path=/test; Secure";
static const char kGetAuthCodeCookieNoOAuthCode[] =
    "Path=/test; Secure; HttpOnly";
static const char kGetTokenPairValidResponse[] =
    "{"
    "  \"refresh_token\": \"rt1\","
    "  \"access_token\": \"at1\","
    "  \"expires_in\": 3600,"
    "  \"token_type\": \"Bearer\""
    "}";
static const char kClientOAuthValidResponse[] =
    "{"
    "  \"oauth2\": {"
    "    \"refresh_token\": \"rt1\","
    "    \"access_token\": \"at1\","
    "    \"expires_in\": 3600,"
    "    \"token_type\": \"Bearer\""
    "  }"
    "}";

static void ExpectCaptchaChallenge(const GoogleServiceAuthError& error) {
  // Make sure this is a captcha server challange.
  EXPECT_EQ(GoogleServiceAuthError::CAPTCHA_REQUIRED, error.state());
  EXPECT_EQ("challengetokenblob", error.captcha().token);
  EXPECT_EQ("http://www.audio.com/", error.captcha().audio_url.spec());
  EXPECT_EQ("http://www.image.com/", error.captcha().image_url.spec());
  EXPECT_EQ(640, error.captcha().image_width);
  EXPECT_EQ(480, error.captcha().image_height);
}

static void ExpectBadAuth(const GoogleServiceAuthError& error) {
  EXPECT_EQ(GoogleServiceAuthError::INVALID_GAIA_CREDENTIALS, error.state());
}

static void ExpectTwoFactorChallenge(const GoogleServiceAuthError& error) {
  // Make sure this is a captcha server challange.
  EXPECT_EQ(GoogleServiceAuthError::TWO_FACTOR, error.state());
  EXPECT_EQ("challengetokenblob", error.second_factor().token);
  EXPECT_EQ("prompt_text", error.second_factor().prompt_text);
  EXPECT_EQ("alternate_text", error.second_factor().alternate_text);
  EXPECT_EQ(10, error.second_factor().field_length);
}

}  // namespace

MockFetcher::MockFetcher(bool success,
                         const GURL& url,
                         const std::string& results,
                         content::URLFetcher::RequestType request_type,
                         content::URLFetcherDelegate* d)
    : TestURLFetcher(0, url, d) {
  set_url(url);
  net::URLRequestStatus::Status code;
  
  if (success) {
    set_response_code(net::HTTP_OK);
    code = net::URLRequestStatus::SUCCESS;
  } else {
    set_response_code(net::HTTP_FORBIDDEN);
    code = net::URLRequestStatus::FAILED;
  }

  set_status(net::URLRequestStatus(code, 0));
  SetResponseString(results);
}

MockFetcher::MockFetcher(const GURL& url,
                         const net::URLRequestStatus& status,
                         int response_code,
                         const net::ResponseCookies& cookies,
                         const std::string& results,
                         content::URLFetcher::RequestType request_type,
                         content::URLFetcherDelegate* d)
    : TestURLFetcher(0, url, d) {
  set_url(url);
  set_status(status);
  set_response_code(response_code);
  set_cookies(cookies);
  SetResponseString(results);
}

MockFetcher::~MockFetcher() {}

void MockFetcher::Start() {
  delegate()->OnURLFetchComplete(this);
}

class GaiaAuthFetcherTest : public testing::Test {
 public:
  GaiaAuthFetcherTest()
      : client_login_source_(GaiaUrls::GetInstance()->client_login_url()),
        issue_auth_token_source_(
            GaiaUrls::GetInstance()->issue_auth_token_url()),
        client_login_to_oauth2_source_(
            GaiaUrls::GetInstance()->client_login_to_oauth2_url()),
        oauth2_token_source_(GaiaUrls::GetInstance()->oauth2_token_url()),
        token_auth_source_(GaiaUrls::GetInstance()->token_auth_url()),
        merge_session_source_(GaiaUrls::GetInstance()->merge_session_url()),
        uberauth_token_source_(base::StringPrintf(
            "%s?source=&issueuberauth=1",
            GaiaUrls::GetInstance()->oauth1_login_url().c_str())),
        client_oauth_source_(GaiaUrls::GetInstance()->client_oauth_url()),
        oauth_login_gurl_(GaiaUrls::GetInstance()->oauth1_login_url()) {}

  void RunParsingTest(const std::string& data,
                      const std::string& sid,
                      const std::string& lsid,
                      const std::string& token) {
    std::string out_sid;
    std::string out_lsid;
    std::string out_token;

    GaiaAuthFetcher::ParseClientLoginResponse(data,
                                              &out_sid,
                                              &out_lsid,
                                              &out_token);
    EXPECT_EQ(lsid, out_lsid);
    EXPECT_EQ(sid, out_sid);
    EXPECT_EQ(token, out_token);
  }

  void RunErrorParsingTest(const std::string& data,
                           const std::string& error,
                           const std::string& error_url,
                           const std::string& captcha_url,
                           const std::string& captcha_token) {
    std::string out_error;
    std::string out_error_url;
    std::string out_captcha_url;
    std::string out_captcha_token;

    GaiaAuthFetcher::ParseClientLoginFailure(data,
                                             &out_error,
                                             &out_error_url,
                                             &out_captcha_url,
                                             &out_captcha_token);
    EXPECT_EQ(error, out_error);
    EXPECT_EQ(error_url, out_error_url);
    EXPECT_EQ(captcha_url, out_captcha_url);
    EXPECT_EQ(captcha_token, out_captcha_token);
  }

  net::ResponseCookies cookies_;
  GURL client_login_source_;
  GURL issue_auth_token_source_;
  GURL client_login_to_oauth2_source_;
  GURL oauth2_token_source_;
  GURL token_auth_source_;
  GURL merge_session_source_;
  GURL uberauth_token_source_;
  GURL client_oauth_source_;
  GURL oauth_login_gurl_;
  TestingProfile profile_;
 protected:
  MessageLoop message_loop_;
};

class MockGaiaConsumer : public GaiaAuthConsumer {
 public:
  MockGaiaConsumer() {}
  ~MockGaiaConsumer() {}

  MOCK_METHOD1(OnClientLoginSuccess, void(const ClientLoginResult& result));
  MOCK_METHOD2(OnIssueAuthTokenSuccess, void(const std::string& service,
      const std::string& token));
  MOCK_METHOD1(OnClientOAuthSuccess,
               void(const GaiaAuthConsumer::ClientOAuthResult& result));
  MOCK_METHOD2(OnTokenAuthSuccess,
               void(const net::ResponseCookies&, const std::string& data));
  MOCK_METHOD1(OnMergeSessionSuccess, void(const std::string& data));
  MOCK_METHOD1(OnUberAuthTokenSuccess, void(const std::string& data));
  MOCK_METHOD1(OnClientLoginFailure,
      void(const GoogleServiceAuthError& error));
  MOCK_METHOD2(OnIssueAuthTokenFailure, void(const std::string& service,
      const GoogleServiceAuthError& error));
  MOCK_METHOD1(OnClientOAuthFailure,
      void(const GoogleServiceAuthError& error));
  MOCK_METHOD1(OnTokenAuthFailure, void(const GoogleServiceAuthError& error));
  MOCK_METHOD1(OnMergeSessionFailure, void(
      const GoogleServiceAuthError& error));
  MOCK_METHOD1(OnUberAuthTokenFailure, void(
      const GoogleServiceAuthError& error));
};

#if defined(OS_WIN)
#define MAYBE_ErrorComparator DISABLED_ErrorComparator
#else
#define MAYBE_ErrorComparator ErrorComparator
#endif

TEST_F(GaiaAuthFetcherTest, MAYBE_ErrorComparator) {
  GoogleServiceAuthError expected_error =
      GoogleServiceAuthError::FromConnectionError(-101);

  GoogleServiceAuthError matching_error =
      GoogleServiceAuthError::FromConnectionError(-101);

  EXPECT_TRUE(expected_error == matching_error);

  expected_error = GoogleServiceAuthError::FromConnectionError(6);

  EXPECT_FALSE(expected_error == matching_error);

  expected_error = GoogleServiceAuthError(GoogleServiceAuthError::NONE);

  EXPECT_FALSE(expected_error == matching_error);

  matching_error = GoogleServiceAuthError(GoogleServiceAuthError::NONE);

  EXPECT_TRUE(expected_error == matching_error);
}

TEST_F(GaiaAuthFetcherTest, LoginNetFailure) {
  int error_no = net::ERR_CONNECTION_RESET;
  net::URLRequestStatus status(net::URLRequestStatus::FAILED, error_no);

  GoogleServiceAuthError expected_error =
      GoogleServiceAuthError::FromConnectionError(error_no);

  MockGaiaConsumer consumer;
  EXPECT_CALL(consumer, OnClientLoginFailure(expected_error))
      .Times(1);

  GaiaAuthFetcher auth(&consumer, std::string(),
      profile_.GetRequestContext());

  MockFetcher mock_fetcher(
      client_login_source_, status, 0, net::ResponseCookies(), std::string(),
      content::URLFetcher::GET, &auth);
  auth.OnURLFetchComplete(&mock_fetcher);
}

TEST_F(GaiaAuthFetcherTest, TokenNetFailure) {
  int error_no = net::ERR_CONNECTION_RESET;
  net::URLRequestStatus status(net::URLRequestStatus::FAILED, error_no);

  GoogleServiceAuthError expected_error =
      GoogleServiceAuthError::FromConnectionError(error_no);

  MockGaiaConsumer consumer;
  EXPECT_CALL(consumer, OnIssueAuthTokenFailure(_, expected_error))
      .Times(1);

  GaiaAuthFetcher auth(&consumer, std::string(),
      profile_.GetRequestContext());

  MockFetcher mock_fetcher(
      issue_auth_token_source_, status, 0, cookies_, std::string(),
      content::URLFetcher::GET, &auth);
  auth.OnURLFetchComplete(&mock_fetcher);
}


TEST_F(GaiaAuthFetcherTest, LoginDenied) {
  std::string data("Error=BadAuthentication");
  net::URLRequestStatus status(net::URLRequestStatus::SUCCESS, 0);

  GoogleServiceAuthError expected_error(
      GoogleServiceAuthError::INVALID_GAIA_CREDENTIALS);

  MockGaiaConsumer consumer;
  EXPECT_CALL(consumer, OnClientLoginFailure(expected_error))
      .Times(1);

  GaiaAuthFetcher auth(&consumer, std::string(),
      profile_.GetRequestContext());

  MockFetcher mock_fetcher(
      client_login_source_, status, net::HTTP_FORBIDDEN, cookies_, data,
      content::URLFetcher::GET, &auth);
  auth.OnURLFetchComplete(&mock_fetcher);
}

TEST_F(GaiaAuthFetcherTest, ParseRequest) {
  RunParsingTest("SID=sid\nLSID=lsid\nAuth=auth\n", "sid", "lsid", "auth");
  RunParsingTest("LSID=lsid\nSID=sid\nAuth=auth\n", "sid", "lsid", "auth");
  RunParsingTest("SID=sid\nLSID=lsid\nAuth=auth", "sid", "lsid", "auth");
  RunParsingTest("SID=sid\nAuth=auth\n", "sid", "", "auth");
  RunParsingTest("LSID=lsid\nAuth=auth\n", "", "lsid", "auth");
  RunParsingTest("\nAuth=auth\n", "", "", "auth");
  RunParsingTest("SID=sid", "sid", "", "");
}

TEST_F(GaiaAuthFetcherTest, ParseErrorRequest) {
  RunErrorParsingTest("Url=U\n"
                      "Error=E\n"
                      "CaptchaToken=T\n"
                      "CaptchaUrl=C\n", "E", "U", "C", "T");
  RunErrorParsingTest("CaptchaToken=T\n"
                      "Error=E\n"
                      "Url=U\n"
                      "CaptchaUrl=C\n", "E", "U", "C", "T");
  RunErrorParsingTest("\n\n\nCaptchaToken=T\n"
                      "\nError=E\n"
                      "\nUrl=U\n"
                      "CaptchaUrl=C\n", "E", "U", "C", "T");
}


TEST_F(GaiaAuthFetcherTest, OnlineLogin) {
  std::string data("SID=sid\nLSID=lsid\nAuth=auth\n");

  GaiaAuthConsumer::ClientLoginResult result;
  result.lsid = "lsid";
  result.sid = "sid";
  result.token = "auth";
  result.data = data;

  MockGaiaConsumer consumer;
  EXPECT_CALL(consumer, OnClientLoginSuccess(result))
      .Times(1);

  GaiaAuthFetcher auth(&consumer, std::string(),
      profile_.GetRequestContext());
  net::URLRequestStatus status(net::URLRequestStatus::SUCCESS, 0);
  MockFetcher mock_fetcher(
      client_login_source_, status, net::HTTP_OK, cookies_, data,
      content::URLFetcher::GET, &auth);
  auth.OnURLFetchComplete(&mock_fetcher);
}

TEST_F(GaiaAuthFetcherTest, WorkingIssueAuthToken) {
  MockGaiaConsumer consumer;
  EXPECT_CALL(consumer, OnIssueAuthTokenSuccess(_, "token"))
      .Times(1);

  GaiaAuthFetcher auth(&consumer, std::string(),
      profile_.GetRequestContext());
  net::URLRequestStatus status(net::URLRequestStatus::SUCCESS, 0);
  MockFetcher mock_fetcher(
      issue_auth_token_source_, status, net::HTTP_OK, cookies_, "token",
      content::URLFetcher::GET, &auth);
  auth.OnURLFetchComplete(&mock_fetcher);
}

TEST_F(GaiaAuthFetcherTest, CheckTwoFactorResponse) {
  std::string response =
      base::StringPrintf("Error=BadAuthentication\n%s\n",
                         GaiaAuthFetcher::kSecondFactor);
  EXPECT_TRUE(GaiaAuthFetcher::IsSecondFactorSuccess(response));
}

TEST_F(GaiaAuthFetcherTest, CheckNormalErrorCode) {
  std::string response = "Error=BadAuthentication\n";
  EXPECT_FALSE(GaiaAuthFetcher::IsSecondFactorSuccess(response));
}

TEST_F(GaiaAuthFetcherTest, TwoFactorLogin) {
  std::string response = base::StringPrintf("Error=BadAuthentication\n%s\n",
      GaiaAuthFetcher::kSecondFactor);

  GoogleServiceAuthError error =
      GoogleServiceAuthError(GoogleServiceAuthError::TWO_FACTOR);

  MockGaiaConsumer consumer;
  EXPECT_CALL(consumer, OnClientLoginFailure(error))
      .Times(1);

  GaiaAuthFetcher auth(&consumer, std::string(),
      profile_.GetRequestContext());
  net::URLRequestStatus status(net::URLRequestStatus::SUCCESS, 0);
  MockFetcher mock_fetcher(
      client_login_source_, status, net::HTTP_FORBIDDEN, cookies_, response,
      content::URLFetcher::GET, &auth);
  auth.OnURLFetchComplete(&mock_fetcher);
}

TEST_F(GaiaAuthFetcherTest, CaptchaParse) {
  net::URLRequestStatus status(net::URLRequestStatus::SUCCESS, 0);
  std::string data = "Url=http://www.google.com/login/captcha\n"
                     "Error=CaptchaRequired\n"
                     "CaptchaToken=CCTOKEN\n"
                     "CaptchaUrl=Captcha?ctoken=CCTOKEN\n";
  GoogleServiceAuthError error =
      GaiaAuthFetcher::GenerateAuthError(data, status);

  std::string token = "CCTOKEN";
  GURL image_url("http://www.google.com/accounts/Captcha?ctoken=CCTOKEN");
  GURL unlock_url("http://www.google.com/login/captcha");

  EXPECT_EQ(error.state(), GoogleServiceAuthError::CAPTCHA_REQUIRED);
  EXPECT_EQ(error.captcha().token, token);
  EXPECT_EQ(error.captcha().image_url, image_url);
  EXPECT_EQ(error.captcha().unlock_url, unlock_url);
}

TEST_F(GaiaAuthFetcherTest, AccountDeletedError) {
  net::URLRequestStatus status(net::URLRequestStatus::SUCCESS, 0);
  std::string data = "Error=AccountDeleted\n";
  GoogleServiceAuthError error =
      GaiaAuthFetcher::GenerateAuthError(data, status);
  EXPECT_EQ(error.state(), GoogleServiceAuthError::ACCOUNT_DELETED);
}

TEST_F(GaiaAuthFetcherTest, AccountDisabledError) {
  net::URLRequestStatus status(net::URLRequestStatus::SUCCESS, 0);
  std::string data = "Error=AccountDisabled\n";
  GoogleServiceAuthError error =
      GaiaAuthFetcher::GenerateAuthError(data, status);
  EXPECT_EQ(error.state(), GoogleServiceAuthError::ACCOUNT_DISABLED);
}

TEST_F(GaiaAuthFetcherTest,BadAuthenticationError) {
  net::URLRequestStatus status(net::URLRequestStatus::SUCCESS, 0);
  std::string data = "Error=BadAuthentication\n";
  GoogleServiceAuthError error =
      GaiaAuthFetcher::GenerateAuthError(data, status);
  EXPECT_EQ(error.state(), GoogleServiceAuthError::INVALID_GAIA_CREDENTIALS);
}

TEST_F(GaiaAuthFetcherTest,IncomprehensibleError) {
  net::URLRequestStatus status(net::URLRequestStatus::SUCCESS, 0);
  std::string data = "Error=Gobbledygook\n";
  GoogleServiceAuthError error =
      GaiaAuthFetcher::GenerateAuthError(data, status);
  EXPECT_EQ(error.state(), GoogleServiceAuthError::SERVICE_UNAVAILABLE);
}

TEST_F(GaiaAuthFetcherTest,ServiceUnavailableError) {
  net::URLRequestStatus status(net::URLRequestStatus::SUCCESS, 0);
  std::string data = "Error=ServiceUnavailable\n";
  GoogleServiceAuthError error =
      GaiaAuthFetcher::GenerateOAuthLoginError(data, status);
  EXPECT_EQ(error.state(), GoogleServiceAuthError::SERVICE_UNAVAILABLE);
}

TEST_F(GaiaAuthFetcherTest, OAuthAccountDeletedError) {
  net::URLRequestStatus status(net::URLRequestStatus::SUCCESS, 0);
  std::string data = "Error=adel\n";
  GoogleServiceAuthError error =
      GaiaAuthFetcher::GenerateOAuthLoginError(data, status);
  EXPECT_EQ(error.state(), GoogleServiceAuthError::ACCOUNT_DELETED);
}

TEST_F(GaiaAuthFetcherTest, OAuthAccountDisabledError) {
  net::URLRequestStatus status(net::URLRequestStatus::SUCCESS, 0);
  std::string data = "Error=adis\n";
  GoogleServiceAuthError error =
      GaiaAuthFetcher::GenerateOAuthLoginError(data, status);
  EXPECT_EQ(error.state(), GoogleServiceAuthError::ACCOUNT_DISABLED);
}

TEST_F(GaiaAuthFetcherTest, OAuthBadAuthenticationError) {
  net::URLRequestStatus status(net::URLRequestStatus::SUCCESS, 0);
  std::string data = "Error=badauth\n";
  GoogleServiceAuthError error =
      GaiaAuthFetcher::GenerateOAuthLoginError(data, status);
  EXPECT_EQ(error.state(), GoogleServiceAuthError::INVALID_GAIA_CREDENTIALS);
}

TEST_F(GaiaAuthFetcherTest, OAuthServiceUnavailableError) {
  net::URLRequestStatus status(net::URLRequestStatus::SUCCESS, 0);
  std::string data = "Error=ire\n";
  GoogleServiceAuthError error =
      GaiaAuthFetcher::GenerateOAuthLoginError(data, status);
  EXPECT_EQ(error.state(), GoogleServiceAuthError::SERVICE_UNAVAILABLE);
}

TEST_F(GaiaAuthFetcherTest, FullLogin) {
  MockGaiaConsumer consumer;
  EXPECT_CALL(consumer, OnClientLoginSuccess(_))
      .Times(1);

  MockURLFetcherFactory<MockFetcher> factory;

  GaiaAuthFetcher auth(&consumer, std::string(),
      profile_.GetRequestContext());
  auth.StartClientLogin("username",
                        "password",
                        "service",
                        std::string(),
                        std::string(),
                        GaiaAuthFetcher::HostedAccountsAllowed);
}

TEST_F(GaiaAuthFetcherTest, FullLoginFailure) {
  MockGaiaConsumer consumer;
  EXPECT_CALL(consumer, OnClientLoginFailure(_))
      .Times(1);

  MockURLFetcherFactory<MockFetcher> factory;
  factory.set_success(false);

  GaiaAuthFetcher auth(&consumer, std::string(),
      profile_.GetRequestContext());
  auth.StartClientLogin("username",
                        "password",
                        "service",
                        std::string(),
                        std::string(),
                        GaiaAuthFetcher::HostedAccountsAllowed);
}

TEST_F(GaiaAuthFetcherTest, ClientFetchPending) {
  MockGaiaConsumer consumer;
  EXPECT_CALL(consumer, OnClientLoginSuccess(_))
      .Times(1);

  TestURLFetcherFactory factory;

  GaiaAuthFetcher auth(&consumer, std::string(),
      profile_.GetRequestContext());
  auth.StartClientLogin("username",
                        "password",
                        "service",
                        std::string(),
                        std::string(),
                        GaiaAuthFetcher::HostedAccountsAllowed);

  EXPECT_TRUE(auth.HasPendingFetch());
  MockFetcher mock_fetcher(
      client_login_source_,
      net::URLRequestStatus(net::URLRequestStatus::SUCCESS, 0),
      net::HTTP_OK, cookies_, "SID=sid\nLSID=lsid\nAuth=auth\n",
      content::URLFetcher::GET, &auth);
  auth.OnURLFetchComplete(&mock_fetcher);
  EXPECT_FALSE(auth.HasPendingFetch());
}

TEST_F(GaiaAuthFetcherTest, FullTokenSuccess) {
  MockGaiaConsumer consumer;
  EXPECT_CALL(consumer, OnIssueAuthTokenSuccess("service", "token"))
      .Times(1);

  TestURLFetcherFactory factory;
  GaiaAuthFetcher auth(&consumer, std::string(),
                       profile_.GetRequestContext());
  auth.StartIssueAuthToken("sid", "lsid", "service");

  EXPECT_TRUE(auth.HasPendingFetch());
  MockFetcher mock_fetcher(
      issue_auth_token_source_,
      net::URLRequestStatus(net::URLRequestStatus::SUCCESS, 0),
      net::HTTP_OK, cookies_, "token",
      content::URLFetcher::GET, &auth);
  auth.OnURLFetchComplete(&mock_fetcher);
  EXPECT_FALSE(auth.HasPendingFetch());
}

TEST_F(GaiaAuthFetcherTest, FullTokenFailure) {
  MockGaiaConsumer consumer;
  EXPECT_CALL(consumer, OnIssueAuthTokenFailure("service", _))
      .Times(1);

  TestURLFetcherFactory factory;

  GaiaAuthFetcher auth(&consumer, std::string(),
      profile_.GetRequestContext());
  auth.StartIssueAuthToken("sid", "lsid", "service");

  EXPECT_TRUE(auth.HasPendingFetch());
  MockFetcher mock_fetcher(
      issue_auth_token_source_,
      net::URLRequestStatus(net::URLRequestStatus::SUCCESS, 0),
      net::HTTP_FORBIDDEN, cookies_, "", content::URLFetcher::GET, &auth);
  auth.OnURLFetchComplete(&mock_fetcher);
  EXPECT_FALSE(auth.HasPendingFetch());
}

TEST_F(GaiaAuthFetcherTest, OAuthLoginTokenSuccess) {
  MockGaiaConsumer consumer;
  EXPECT_CALL(consumer, OnClientOAuthSuccess(
      GaiaAuthConsumer::ClientOAuthResult("rt1", "at1", 3600))).Times(1);

  TestURLFetcherFactory factory;
  GaiaAuthFetcher auth(&consumer, std::string(),
                       profile_.GetRequestContext());
  auth.StartLsoForOAuthLoginTokenExchange("lso_token");
  TestURLFetcher* fetcher = factory.GetFetcherByID(0);
  EXPECT_TRUE(NULL != fetcher);
  EXPECT_EQ(net::LOAD_DO_NOT_SEND_COOKIES | net::LOAD_DO_NOT_SAVE_COOKIES,
            fetcher->GetLoadFlags());

  net::ResponseCookies cookies;
  cookies.push_back(kGetAuthCodeValidCookie);
  EXPECT_TRUE(auth.HasPendingFetch());
  MockFetcher mock_fetcher1(
      client_login_to_oauth2_source_,
      net::URLRequestStatus(net::URLRequestStatus::SUCCESS, 0),
      net::HTTP_OK, cookies, "",
      content::URLFetcher::POST, &auth);
  auth.OnURLFetchComplete(&mock_fetcher1);
  EXPECT_TRUE(auth.HasPendingFetch());
  MockFetcher mock_fetcher2(
      oauth2_token_source_,
      net::URLRequestStatus(net::URLRequestStatus::SUCCESS, 0),
      net::HTTP_OK, cookies_, kGetTokenPairValidResponse,
      content::URLFetcher::POST, &auth);
  auth.OnURLFetchComplete(&mock_fetcher2);
  EXPECT_FALSE(auth.HasPendingFetch());
}

TEST_F(GaiaAuthFetcherTest, OAuthLoginTokenWithCookies) {
  MockGaiaConsumer consumer;
  TestURLFetcherFactory factory;
  GaiaAuthFetcher auth(&consumer, std::string(),
                       profile_.GetRequestContext());
  auth.StartCookieForOAuthLoginTokenExchange("0");
  TestURLFetcher* fetcher = factory.GetFetcherByID(0);
  EXPECT_TRUE(NULL != fetcher);
  EXPECT_EQ(net::LOAD_NORMAL, fetcher->GetLoadFlags());
}

TEST_F(GaiaAuthFetcherTest, OAuthLoginTokenClientLoginToOAuth2Failure) {
  MockGaiaConsumer consumer;
  EXPECT_CALL(consumer, OnClientOAuthFailure(_))
      .Times(1);

  TestURLFetcherFactory factory;
  GaiaAuthFetcher auth(&consumer, std::string(),
                       profile_.GetRequestContext());
  auth.StartLsoForOAuthLoginTokenExchange("lso_token");

  net::ResponseCookies cookies;
  EXPECT_TRUE(auth.HasPendingFetch());
  MockFetcher mock_fetcher(
      client_login_to_oauth2_source_,
      net::URLRequestStatus(net::URLRequestStatus::SUCCESS, 0),
      net::HTTP_FORBIDDEN, cookies, "",
      content::URLFetcher::POST, &auth);
  auth.OnURLFetchComplete(&mock_fetcher);
  EXPECT_FALSE(auth.HasPendingFetch());
}

TEST_F(GaiaAuthFetcherTest, OAuthLoginTokenOAuth2TokenPairFailure) {
  MockGaiaConsumer consumer;
  EXPECT_CALL(consumer, OnClientOAuthFailure(_))
      .Times(1);

  TestURLFetcherFactory factory;
  GaiaAuthFetcher auth(&consumer, std::string(),
                       profile_.GetRequestContext());
  auth.StartLsoForOAuthLoginTokenExchange("lso_token");

  net::ResponseCookies cookies;
  cookies.push_back(kGetAuthCodeValidCookie);
  EXPECT_TRUE(auth.HasPendingFetch());
  MockFetcher mock_fetcher1(
      client_login_to_oauth2_source_,
      net::URLRequestStatus(net::URLRequestStatus::SUCCESS, 0),
      net::HTTP_OK, cookies, "",
      content::URLFetcher::POST, &auth);
  auth.OnURLFetchComplete(&mock_fetcher1);
  EXPECT_TRUE(auth.HasPendingFetch());
  MockFetcher mock_fetcher2(
      oauth2_token_source_,
      net::URLRequestStatus(net::URLRequestStatus::SUCCESS, 0),
      net::HTTP_FORBIDDEN, cookies_, "",
      content::URLFetcher::POST, &auth);
  auth.OnURLFetchComplete(&mock_fetcher2);
  EXPECT_FALSE(auth.HasPendingFetch());
}

TEST_F(GaiaAuthFetcherTest, TokenAuthSuccess) {
  MockGaiaConsumer consumer;
  net::ResponseCookies cookies;
  EXPECT_CALL(consumer, OnTokenAuthSuccess(cookies, "<html></html>"))
      .Times(1);

  TestURLFetcherFactory factory;

  GaiaAuthFetcher auth(&consumer, std::string(),
      profile_.GetRequestContext());
  auth.StartTokenAuth("myubertoken");

  EXPECT_TRUE(auth.HasPendingFetch());
  MockFetcher mock_fetcher(
      token_auth_source_,
      net::URLRequestStatus(net::URLRequestStatus::SUCCESS, 0),
      net::HTTP_OK, cookies_, "<html></html>", content::URLFetcher::GET,
      &auth);
  auth.OnURLFetchComplete(&mock_fetcher);
  EXPECT_FALSE(auth.HasPendingFetch());
}

TEST_F(GaiaAuthFetcherTest, TokenAuthUnauthorizedFailure) {
  MockGaiaConsumer consumer;
  EXPECT_CALL(consumer, OnTokenAuthFailure(_))
      .Times(1);

  TestURLFetcherFactory factory;

  GaiaAuthFetcher auth(&consumer, std::string(),
      profile_.GetRequestContext());
  auth.StartTokenAuth("badubertoken");

  EXPECT_TRUE(auth.HasPendingFetch());
  MockFetcher mock_fetcher(
      token_auth_source_,
      net::URLRequestStatus(net::URLRequestStatus::SUCCESS, 0),
      net::HTTP_UNAUTHORIZED, cookies_, "", content::URLFetcher::GET, &auth);
  auth.OnURLFetchComplete(&mock_fetcher);
  EXPECT_FALSE(auth.HasPendingFetch());
}

TEST_F(GaiaAuthFetcherTest, TokenAuthNetFailure) {
  MockGaiaConsumer consumer;
  EXPECT_CALL(consumer, OnTokenAuthFailure(_))
      .Times(1);

  TestURLFetcherFactory factory;

  GaiaAuthFetcher auth(&consumer, std::string(),
      profile_.GetRequestContext());
  auth.StartTokenAuth("badubertoken");

  EXPECT_TRUE(auth.HasPendingFetch());
  MockFetcher mock_fetcher(
      token_auth_source_,
      net::URLRequestStatus(net::URLRequestStatus::FAILED, 0),
      net::HTTP_OK, cookies_, "", content::URLFetcher::GET, &auth);
  auth.OnURLFetchComplete(&mock_fetcher);
  EXPECT_FALSE(auth.HasPendingFetch());
}

TEST_F(GaiaAuthFetcherTest, MergeSessionSuccess) {
  MockGaiaConsumer consumer;
  EXPECT_CALL(consumer, OnMergeSessionSuccess("<html></html>"))
      .Times(1);

  TestURLFetcherFactory factory;

  GaiaAuthFetcher auth(&consumer, std::string(),
      profile_.GetRequestContext());
  auth.StartMergeSession("myubertoken");

  EXPECT_TRUE(auth.HasPendingFetch());
  MockFetcher mock_fetcher(
      merge_session_source_,
      net::URLRequestStatus(net::URLRequestStatus::SUCCESS, 0),
      net::HTTP_OK, cookies_, "<html></html>", content::URLFetcher::GET,
      &auth);
  auth.OnURLFetchComplete(&mock_fetcher);
  EXPECT_FALSE(auth.HasPendingFetch());
}

TEST_F(GaiaAuthFetcherTest, MergeSessionSuccessRedirect) {
  MockGaiaConsumer consumer;
  EXPECT_CALL(consumer, OnMergeSessionSuccess("<html></html>"))
      .Times(1);

  TestURLFetcherFactory factory;

  GaiaAuthFetcher auth(&consumer, std::string(),
      profile_.GetRequestContext());
  auth.StartMergeSession("myubertoken");

  // Make sure the fetcher created has the expected flags.  Set its url()
  // properties to reflect a redirect.
  TestURLFetcher* test_fetcher = factory.GetFetcherByID(0);
  EXPECT_TRUE(test_fetcher != NULL);
  EXPECT_TRUE(test_fetcher->GetLoadFlags() == net::LOAD_NORMAL);
  EXPECT_TRUE(auth.HasPendingFetch());

  GURL final_url("http://www.google.com/CheckCookie");
  test_fetcher->set_url(final_url);
  test_fetcher->set_status(
      net::URLRequestStatus(net::URLRequestStatus::SUCCESS, 0));
  test_fetcher->set_response_code(net::HTTP_OK);
  test_fetcher->set_cookies(cookies_);
  test_fetcher->SetResponseString("<html></html>");

  auth.OnURLFetchComplete(test_fetcher);
  EXPECT_FALSE(auth.HasPendingFetch());
}

TEST_F(GaiaAuthFetcherTest, UberAuthTokenSuccess) {
  MockGaiaConsumer consumer;
  EXPECT_CALL(consumer, OnUberAuthTokenSuccess("uberToken"))
      .Times(1);

  TestURLFetcherFactory factory;

  GaiaAuthFetcher auth(&consumer, std::string(),
      profile_.GetRequestContext());
  auth.StartTokenFetchForUberAuthExchange("myAccessToken");

  EXPECT_TRUE(auth.HasPendingFetch());
  MockFetcher mock_fetcher(
      uberauth_token_source_,
      net::URLRequestStatus(net::URLRequestStatus::SUCCESS, 0),
      net::HTTP_OK, cookies_, "uberToken", content::URLFetcher::POST,
      &auth);
  auth.OnURLFetchComplete(&mock_fetcher);
  EXPECT_FALSE(auth.HasPendingFetch());
}

TEST_F(GaiaAuthFetcherTest, ParseClientLoginToOAuth2Response) {
  {  // No cookies.
    std::string auth_code;
    net::ResponseCookies cookies;
    EXPECT_FALSE(GaiaAuthFetcher::ParseClientLoginToOAuth2Response(
        cookies, &auth_code));
    EXPECT_EQ("", auth_code);
  }
  {  // Few cookies, nothing appropriate.
    std::string auth_code;
    net::ResponseCookies cookies;
    cookies.push_back(kGetAuthCodeCookieNoSecure);
    cookies.push_back(kGetAuthCodeCookieNoHttpOnly);
    cookies.push_back(kGetAuthCodeCookieNoOAuthCode);
    EXPECT_FALSE(GaiaAuthFetcher::ParseClientLoginToOAuth2Response(
        cookies, &auth_code));
    EXPECT_EQ("", auth_code);
  }
  {  // Few cookies, one of them is valid.
    std::string auth_code;
    net::ResponseCookies cookies;
    cookies.push_back(kGetAuthCodeCookieNoSecure);
    cookies.push_back(kGetAuthCodeCookieNoHttpOnly);
    cookies.push_back(kGetAuthCodeCookieNoOAuthCode);
    cookies.push_back(kGetAuthCodeValidCookie);
    EXPECT_TRUE(GaiaAuthFetcher::ParseClientLoginToOAuth2Response(
        cookies, &auth_code));
    EXPECT_EQ("test-code", auth_code);
  }
  {  // Single valid cookie (like in real responses).
    std::string auth_code;
    net::ResponseCookies cookies;
    cookies.push_back(kGetAuthCodeValidCookie);
    EXPECT_TRUE(GaiaAuthFetcher::ParseClientLoginToOAuth2Response(
        cookies, &auth_code));
    EXPECT_EQ("test-code", auth_code);
  }
}

TEST_F(GaiaAuthFetcherTest, ClientOAuthSuccess) {
  MockURLFetcherFactory<MockFetcher> factory;
  factory.set_results(kClientOAuthValidResponse);

  MockGaiaConsumer consumer;
  EXPECT_CALL(consumer, OnClientOAuthSuccess(
      GaiaAuthConsumer::ClientOAuthResult("rt1", "at1", 3600))).Times(1);

  GaiaAuthFetcher auth(&consumer, "tests", profile_.GetRequestContext());
  std::vector<std::string> scopes;
  scopes.push_back(GaiaUrls::GetInstance()->oauth1_login_scope());
  scopes.push_back("https://some.other.scope.com");
  auth.StartClientOAuth("username", "password", scopes, "", "en");

  scoped_ptr<base::Value> actual(base::JSONReader::Read(auth.request_body_));
  scoped_ptr<base::Value> expected(base::JSONReader::Read(
      "{"
      "\"email\": \"username\","
      "\"password\": \"password\","
      "\"scopes\": [\"https://www.google.com/accounts/OAuthLogin\","
      "             \"https://some.other.scope.com\"],"
      "\"oauth2_client_id\": \"77185425430.apps.googleusercontent.com\","
      "\"friendly_device_name\": \"tests\","
      "\"accepts_challenges\": [\"Captcha\", \"TwoStep\"],"
      "\"locale\": \"en\","
      "\"fallback\": { \"name\": \"GetOAuth2Token\" }"
      "}"));
  EXPECT_TRUE(expected->Equals(actual.get()));
}

TEST_F(GaiaAuthFetcherTest, ClientOAuthBadAuth) {
  MockURLFetcherFactory<MockFetcher> factory;
  factory.set_success(false);
  factory.set_results("{"
                      "  \"cause\" : \"BadAuthentication\","
                      "  \"fallback\" : {"
                      "    \"name\" : \"Terminating\","
                      "    \"url\" : \"https://www.terminating.com\""
                      "  }"
                      "}");

  MockGaiaConsumer consumer;
  EXPECT_CALL(consumer, OnClientOAuthFailure(_))
      .WillOnce(Invoke(ExpectBadAuth));

  GaiaAuthFetcher auth(&consumer, "tests", profile_.GetRequestContext());
  std::vector<std::string> scopes;
  scopes.push_back(GaiaUrls::GetInstance()->oauth1_login_scope());
  auth.StartClientOAuth("username", "password", scopes, "", "en");
}

TEST_F(GaiaAuthFetcherTest, ClientOAuthCaptchaChallenge) {
  MockURLFetcherFactory<MockFetcher> factory;
  factory.set_success(false);
  factory.set_results("{"
                      "  \"cause\" : \"NeedsAdditional\","
                      "  \"fallback\" : {"
                      "    \"name\" : \"Terminating\","
                      "    \"url\" : \"https://www.terminating.com\""
                      "  },"
                      "  \"challenge\" : {"
                      "    \"name\" : \"Captcha\","
                      "    \"image_url\" : \"http://www.image.com/\","
                      "    \"image_width\" : 640,"
                      "    \"image_height\" : 480,"
                      "    \"audio_url\" : \"http://www.audio.com/\","
                      "    \"challenge_token\" : \"challengetokenblob\""
                      "  }"
                      "}");

  MockGaiaConsumer consumer;
  EXPECT_CALL(consumer, OnClientOAuthFailure(_))
      .WillOnce(Invoke(ExpectCaptchaChallenge));

  GaiaAuthFetcher auth(&consumer, "tests", profile_.GetRequestContext());
  std::vector<std::string> scopes;
  scopes.push_back(GaiaUrls::GetInstance()->oauth1_login_scope());
  auth.StartClientOAuth("username", "password", scopes, "", "en");
}

TEST_F(GaiaAuthFetcherTest, ClientOAuthTwoFactorChallenge) {
  MockURLFetcherFactory<MockFetcher> factory;
  factory.set_success(false);
  factory.set_results("{"
                      "  \"cause\" : \"NeedsAdditional\","
                      "  \"fallback\" : {"
                      "    \"name\" : \"Terminating\","
                      "    \"url\" : \"https://www.terminating.com\""
                      "  },"
                      "  \"challenge\" : {"
                      "    \"name\" : \"TwoFactor\","
                      "    \"prompt_text\" : \"prompt_text\","
                      "    \"alternate_text\" : \"alternate_text\","
                      "    \"challenge_token\" : \"challengetokenblob\","
                      "    \"field_length\" : 10"
                      "  }"
                      "}");

  MockGaiaConsumer consumer;
  EXPECT_CALL(consumer, OnClientOAuthFailure(_))
      .WillOnce(Invoke(ExpectTwoFactorChallenge));

  GaiaAuthFetcher auth(&consumer, "tests", profile_.GetRequestContext());
  std::vector<std::string> scopes;
  scopes.push_back(GaiaUrls::GetInstance()->oauth1_login_scope());
  auth.StartClientOAuth("username", "password", scopes, "", "en");
}

TEST_F(GaiaAuthFetcherTest, ClientOAuthChallengeSuccess) {
  MockURLFetcherFactory<MockFetcher> factory;
  factory.set_results(kClientOAuthValidResponse);

  MockGaiaConsumer consumer;
  EXPECT_CALL(consumer, OnClientOAuthSuccess(
      GaiaAuthConsumer::ClientOAuthResult("rt1", "at1", 3600))).Times(2);

  GaiaAuthFetcher auth1(&consumer, std::string(), profile_.GetRequestContext());
  auth1.StartClientOAuthChallengeResponse(GoogleServiceAuthError::TWO_FACTOR,
                                          "token", "mysolution");

  scoped_ptr<base::Value> actual1(base::JSONReader::Read(auth1.request_body_));
  scoped_ptr<base::Value> expected1(base::JSONReader::Read(
      "{"
      "  \"challenge_reply\" : {"
      "    \"name\" : \"TwoFactor\","
      "    \"challenge_token\" : \"token\","
      "    \"otp\" : \"mysolution\""
      "  }"
      "}"));
  EXPECT_TRUE(expected1->Equals(actual1.get()));

  GaiaAuthFetcher auth2(&consumer, "tests", profile_.GetRequestContext());
  auth2.StartClientOAuthChallengeResponse(
      GoogleServiceAuthError::CAPTCHA_REQUIRED, "token", "mysolution");

  scoped_ptr<base::Value> actual2(base::JSONReader::Read(auth2.request_body_));
  scoped_ptr<base::Value> expected2(base::JSONReader::Read(
      "{"
      "  \"challenge_reply\" : {"
      "    \"name\" : \"Captcha\","
      "    \"challenge_token\" : \"token\","
      "    \"solution\" : \"mysolution\""
      "  }"
      "}"));
  EXPECT_TRUE(expected2->Equals(actual2.get()));
}

TEST_F(GaiaAuthFetcherTest, StartOAuthLogin) {
  // OAuthLogin returns the same as the ClientLogin endpoint, minus CAPTCHA
  // responses.
  std::string data("SID=sid\nLSID=lsid\nAuth=auth\n");

  GaiaAuthConsumer::ClientLoginResult result;
  result.lsid = "lsid";
  result.sid = "sid";
  result.token = "auth";
  result.data = data;

  MockGaiaConsumer consumer;
  EXPECT_CALL(consumer, OnClientLoginSuccess(result))
      .Times(1);

  GaiaAuthFetcher auth(&consumer, std::string(),
      profile_.GetRequestContext());
  net::URLRequestStatus status(net::URLRequestStatus::SUCCESS, 0);
  MockFetcher mock_fetcher(
      oauth_login_gurl_, status, net::HTTP_OK, cookies_, data,
      content::URLFetcher::GET, &auth);
  auth.OnURLFetchComplete(&mock_fetcher);
}
