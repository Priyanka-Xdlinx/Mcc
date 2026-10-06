import { Component } from "react";
import ErrorFallback from "./ErrorFallback.jsx";
import logger from "../utils/logger.js";

// React only supports error boundaries as class components.
export default class ErrorBoundary extends Component {
  state = { hasError: false, retriedOnce: false };

  static getDerivedStateFromError() {
    return { hasError: true };
  }

  componentDidCatch(error, info) {
    logger.error("Unhandled render error", error, info);
  }

  reset = () => {
    this.setState({ hasError: false, retriedOnce: true });
  };

  render() {
    if (!this.state.hasError) return this.props.children;
    return <ErrorFallback onRetry={this.reset} canReload={this.state.retriedOnce} />;
  }
}
